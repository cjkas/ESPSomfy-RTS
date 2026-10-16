#include "esp_log.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "ConfigSettings.h"
#include "ESPNetwork.h"
#include "entryPoints/Web.h"
#include "outputs/Sockets.h"
#include "Utils.h"
#include "Somfy.h"
#include "entryPoints/MQTT.h"
#include "LoopQueue.h"
#include "ShadeRequests.h"
#include "SomfyShadeOps.h"
#include "ModelSnapshot.h"
#include "GitOTA.h"
#include "esp_core_dump.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_app_desc.h"
#define getRunningElfSha esp_app_get_elf_sha256
#else
#include "esp_ota_ops.h"
#define getRunningElfSha esp_ota_get_app_elf_sha256
#endif
#include "Timing.h"

static const char *TAG = "Main";
#define LOOP_SLOW_MS 100

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
ESPNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;
SomfyShadeOps shadeOps;
ShadeRequests shadeRequests(shadeOps);
LoopQueue loopQueue;
SnapshotSlots snapshotSlots;

uint32_t oldheap = 0;

void listDir(const char *dirname, uint8_t levels) {
    ESP_LOGI(TAG, "Listing: %s", dirname);
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            ESP_LOGI(TAG, "  DIR : %s", file.name());
            if (levels) listDir(file.path(), levels - 1);
        } else {
            ESP_LOGI(TAG, "  FILE: %-30s  %d bytes", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {  
  Serial.begin(115200);
  ESP_LOGI(TAG, "Startup/Boot....");
  esp_core_dump_summary_t summary;
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    // The dump stays in flash until the next crash replaces it, so it can still
    // be read later with espcoredump.py.  Only shout about it on the boot right
    // after the crash.
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool newCrash = reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT ||
      reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
#define CRASH_LOG(fmt, ...) do { if(newCrash) ESP_LOGW(TAG, fmt, ##__VA_ARGS__); else ESP_LOGI(TAG, fmt, ##__VA_ARGS__); } while(0)
    if(newCrash) ESP_LOGW(TAG, "*** Crash coredump from the last boot ***");
    else ESP_LOGI(TAG, "Stored coredump from an earlier crash");
    CRASH_LOG("  Task: %s", summary.exc_task);
    CRASH_LOG("  PC: 0x%08x", summary.exc_pc);
#ifdef CONFIG_IDF_TARGET_ARCH_XTENSA
    CRASH_LOG("  Cause: %d", summary.ex_info.exc_cause);
    char bt_buf[256] = {0};
    int pos = 0;
    for (int i = 0; i < summary.exc_bt_info.depth; i++) {
      pos += snprintf(bt_buf + pos, sizeof(bt_buf) - pos, " 0x%08x", summary.exc_bt_info.bt[i]);
    }
    CRASH_LOG("  Backtrace:%s", bt_buf);
#elif CONFIG_IDF_TARGET_ARCH_RISCV
    CRASH_LOG("  Cause: %d", summary.ex_info.mcause);
    CRASH_LOG("  MTVAL: 0x%08x  RA: 0x%08x  SP: 0x%08x",
             summary.ex_info.mtval, summary.ex_info.ra, summary.ex_info.sp);
#endif
    // The backtrace only decodes against the firmware.elf of the build that
    // crashed, so say whether that is the one running now.
    // app_elf_sha256 isn't always terminated, so copy it before comparing or
    // printing it.
    char crashedSha[APP_ELF_SHA256_SZ];
    static_assert(sizeof(crashedSha) <= sizeof(summary.app_elf_sha256), "copy would read past app_elf_sha256");
    SETCHARPROP(crashedSha, (const char *)summary.app_elf_sha256, sizeof(crashedSha));
    char runningSha[APP_ELF_SHA256_SZ] = {0};
    getRunningElfSha(runningSha, sizeof(runningSha));
    if(strcmp(crashedSha, runningSha) == 0)
      CRASH_LOG("  Build: %s (running %s, same build)", crashedSha, runningSha);
    else
      CRASH_LOG("  Build: %s (running %s, different build, decode the backtrace with the matching firmware.elf)", crashedSha, runningSha);
#undef CRASH_LOG
  }
  ESP_LOGI(TAG, "Mounting File System...");
  if (LittleFS.begin()) {
      ESP_LOGI(TAG, "Total: %d bytes", LittleFS.totalBytes());
      ESP_LOGI(TAG, "Used:  %d bytes", LittleFS.usedBytes());
      ESP_LOGI(TAG, "Free:  %d bytes", LittleFS.totalBytes() - LittleFS.usedBytes());
      listDir("/", 3);
      ESP_LOGI(TAG, "File system mounted successfully");
  } else {
      ESP_LOGE(TAG, "LittleFS mount failed!");
  }
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  delay(10);
  // Before anything that can submit to it: the web servers and MQTT.
  loopQueue.begin();
  webServer.startup();
  webServer.begin();
  delay(1000);
  net.setup();  
  somfy.begin();
  //git.checkForUpdate();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  const esp_task_wdt_config_t wdt_config = { .timeout_ms = 15000, .idle_core_mask = 1, .trigger_panic = true };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(15, true); //enable panic so ESP32 restarts
#endif
  esp_task_wdt_add(NULL); //add current thread to WDT watch

}

void loop() {
  // put your main code here, to run repeatedly:
  //uint32_t heap = ESP.getFreeHeap();
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    ESP_LOGI(TAG, "Rebooting after %lums", rebootDelay.rebootTime);
    net.end();
    ESP.restart();
    return;
  }
  // Each step only logs when it holds up the loop for LOOP_SLOW_MS or more.
  ElapsedTimer timing;
  net.loop();
  logIfSlow(TAG, "Loop net", timing.lap(), LOOP_SLOW_MS);
  esp_task_wdt_reset();
  somfy.loop();
  logIfSlow(TAG, "Loop somfy", timing.lap(), LOOP_SLOW_MS);
  esp_task_wdt_reset();
  loopQueue.drain();
  logIfSlow(TAG, "Loop queue", timing.lap(), LOOP_SLOW_MS);
  esp_task_wdt_reset();
  // What changed in this pass goes out once, to MQTT and the sockets.
  somfy.sendChanges();
  logIfSlow(TAG, "Loop changes", timing.lap(), LOOP_SLOW_MS);
  esp_task_wdt_reset();
  if(net.connected() || net.softAPOpened) {
    if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
      git.loop();
      logIfSlow(TAG, "Loop git", timing.lap(), LOOP_SLOW_MS);
      esp_task_wdt_reset();
    }
    webServer.loop();
    logIfSlow(TAG, "Loop web server", timing.lap(), LOOP_SLOW_MS);
    esp_task_wdt_reset();
    sockEmit.loop();
    logIfSlow(TAG, "Loop sockets", timing.lap(), LOOP_SLOW_MS);
    esp_task_wdt_reset();
  }
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
  }
  esp_task_wdt_reset();
}
