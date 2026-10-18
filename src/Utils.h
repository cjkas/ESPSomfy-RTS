#ifndef utils_h
#define utils_h
#include <Arduino.h>
#include <atomic>


#define DEBUG_SOMFY Serial




[[maybe_unused]] static void SETCHARPROP(char *prop, const char *value, size_t size) {strncpy(prop, value, size); prop[size - 1] = '\0';}
/*
namespace util { 
  // Createa a custom to_string function.  C++ can be annoying
  // with all the trailing 0s on number formats.
  template <typename T> std::string to_string(const T& t) {
    std::string str{std::to_string (t)};
    int offset{1};
    if (str.find_last_not_of('0') == str.find('.')) {
      offset = 0;     
    }
    str.erase ( str.find_last_not_of('0') + 1, std::string::npos ); 
    str.erase ( str.find_last_not_of('.') + 1, std::string::npos );    
    return str; 
  } 
}
*/

static void _ltrim(char *str) {
  int s = 0, j, k = 0;
  int e = strlen(str);
  while(s < e && (str[s] == ' ' || str[s] == '\n' || str[s] == '\r' || str[s] == '\t' || str[s] == '"')) s++;
  if(s > 0) {
    for(j = s; j < e; j++) {
      str[k] = str[j];
      k++;
    }
    str[k] = '\0';
  }
  //if(s > 0) strcpy(str, &str[s]);
}
static void _rtrim(char *str) {
  int e = strlen(str) - 1;
  while(e >= 0 && (str[e] == ' ' || str[e] == '\n' || str[e] == '\r' || str[e] == '\t' || str[e] == '"')) {str[e] = '\0'; e--;}
}
[[maybe_unused]] static void _trim(char *str) { _ltrim(str); _rtrim(str); }
struct rebootDelay_t {
  bool reboot = false;
  int rebootTime = 0;
  bool closed = false;
};
[[maybe_unused]] static bool toBoolean(const char *str, bool def) {
  if(!str) return def;
  if(strlen(str) == 0) return def;
  else if(str[0] == 't' || str[0] == 'T' || str[0] == '1') return true;
  else if(str[0] == 'f' || str[0] == 'F' || str[0] == '0') return false;
  return def;
}
// Whether a wifiStrength broadcast is worth sending: always on a channel change,
// otherwise only for an RSSI change of at least minDelta dBm and not more often
// than every minMs.
bool rssiNeedsEmit(int rssi, int lastRssi, int channel, int lastChannel, uint32_t sinceLastMs, int minDelta, uint32_t minMs);
// What the last wifiStrength broadcast told every client.  Only broadcasts
// move it: an emit to one new client tells the others nothing.
struct RssiBroadcast {
  int rssi = 0;
  int channel = 0;
  uint32_t at = 0;  // millis() of the last broadcast.
  bool due(int rssi, int channel, uint32_t now, int minDelta, uint32_t minMs) const;
  void sent(bool broadcast, int rssi, int channel, uint32_t now);
};
// Whether an AP a scan found for our SSID is worth connecting to.  While
// connected it has to be at least minGain dBm stronger than the current AP so
// a small swing doesn't make the device hop between APs.
bool worthRoaming(bool connected, int currentRssi, int apRssi, int minGain);
// Who gets a memStatus event.
enum class heap_emit_t : uint8_t { none, client, broadcast, room };
// num is the socket client asked for, 255 for everyone.  A broadcast goes out
// every intervalMs; in between only the clients in room 0 get one.
heap_emit_t heapEmitTarget(uint8_t num, uint32_t sinceLastMs, uint32_t intervalMs, bool roomHasClients);
// How well loop() keeps up: the passes it made per second and its longest
// pass, over the window since the last memStatus broadcast.  Only the loop
// task calls record(), rate(), maxUs() and publish().  publish() closes the
// window and keeps its figures where another task (the /discovery response)
// can read them without seeing a half reset window.
class LoopStats {
  uint32_t _passes = 0;
  uint32_t _maxUs = 0;
  uint32_t _startMs = 0;
  std::atomic<uint32_t> _publishedRate{0};
  std::atomic<uint32_t> _publishedMaxUs{0};
  public:
    void record(uint32_t passUs);
    // Passes per second since the window started, 0 for an empty window.
    uint32_t rate(uint32_t nowMs) const;
    uint32_t maxUs() const { return _maxUs; }
    void publish(uint32_t nowMs);
    uint32_t publishedRate() const { return _publishedRate.load(std::memory_order_relaxed); }
    uint32_t publishedMaxUs() const { return _publishedMaxUs.load(std::memory_order_relaxed); }
};

class Timestamp {
  char _timeBuffer[128];
  public:
    time_t getUTC();
    time_t getUTC(time_t epoch);
    char * getISOTime();
    char * getISOTime(time_t epoch);
    char * formatISO(struct tm *dt, int tz);
    int tzOffset();
    static time_t parseUTCTime(const char *buff);
    static time_t mkUTCTime(struct tm *dt);
    static int calcTZOffset(time_t *dt);
    static time_t now();
    static unsigned long epoch();
};
// Sort an array
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray);
// Sort in reverse
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray);
// Sort an array with custom comparison function
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType));
// Sort in reverse with custom comparison function
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType));
namespace ArduinoSort {
  template<typename AnyType> bool builtinLargerThan(AnyType first, AnyType second) { return first > second; }
  //template<> bool builtinLargerThan(char* first, char* second) { return strcmp(first, second) > 0; }
  template<typename AnyType> void insertionSort(AnyType array[], size_t sizeOfArray, bool reverse, bool (*largerThan)(AnyType, AnyType)) { for (size_t i = 1; i < sizeOfArray; i++) {
    for (size_t j = i; j > 0 && (largerThan(array[j-1], array[j]) != reverse); j--) {
        AnyType tmp = array[j-1];
        array[j-1] = array[j];
        array[j] = tmp;
      }
    }
  }
}
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray) { ArduinoSort::insertionSort(array, sizeOfArray, false, ArduinoSort::builtinLargerThan); }
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray) { ArduinoSort::insertionSort(array, sizeOfArray, true, ArduinoSort::builtinLargerThan); }
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType)) { ArduinoSort::insertionSort(array, sizeOfArray, false, largerThan); }
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType)) { ArduinoSort::insertionSort(array, sizeOfArray, true, largerThan); }
#endif
