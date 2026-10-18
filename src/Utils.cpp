#include <Arduino.h>
#include <time.h>
#include "Utils.h"


/*********************************************************************
 * Timestamp class members
 ********************************************************************/
unsigned long Timestamp::epoch() {
  struct tm tmNow;
  time_t now;
  if(!getLocalTime(&tmNow,50)) return 0;
  time(&now);
  return now;
}
time_t Timestamp::now() {
  struct tm tmNow;
  getLocalTime(&tmNow,50);
  return mktime(&tmNow);
}
time_t Timestamp::getUTC() { 
  time_t t;
  time(&t);
  return t; 
}
time_t Timestamp::mkUTCTime(struct tm *dt) {
  time_t tsBadLocal = mktime(dt);

  struct tm tmUTC;
  struct tm tmLocal;
  gmtime_r(&tsBadLocal, &tmUTC);
  localtime_r(&tsBadLocal, &tmLocal);
  time_t tsBadUTC = mktime(&tmUTC);
  time_t tsLocal = mktime(&tmLocal);
  time_t tsLocalOffset = tsLocal - tsBadUTC;
  return tsBadLocal + tsLocalOffset;
}
time_t Timestamp::parseUTCTime(const char *buff) {
  struct tm dt;
  dt.tm_hour = 0;
  dt.tm_mday = 0;
  dt.tm_mon = 0;
  dt.tm_year = 0;
  dt.tm_wday = 0;
  dt.tm_yday = 0;
  dt.tm_isdst = false;
  char num[5];
  uint8_t i = 0;
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_year = atoi(num)-1900;
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_mon = atoi(num)-1;  
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == 'T' || ch == 't') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_mday = atoi(num);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_hour = atoi(num);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_min = atoi(num);
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':' || ch == 'Z') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_sec = atoi(num);
  //Serial.printf("Y:%d M:%d D:%d H:%d M:%d S:%d\n", dt.tm_year, dt.tm_mon, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
  return Timestamp::mkUTCTime(&dt);
}
time_t Timestamp::getUTC(time_t t) {
  tm tmUTC;
  gmtime_r(&t, &tmUTC);
  return mktime(&tmUTC);
}
char * Timestamp::getISOTime() { return this->getISOTime(this->getUTC()); }
char * Timestamp::getISOTime(time_t epoch) {
  struct tm *dt = localtime((time_t *)&epoch);
  return this->formatISO(dt, this->tzOffset());
}
char * Timestamp::formatISO(struct tm *dt, int tz) {
  int tzHrs = floor(tz/100);
  int tzMin = tz - (tzHrs * 100);
  int ms = millis() % 1000;
  snprintf(this->_timeBuffer, sizeof(this->_timeBuffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%s%02d%02d", 
    dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday, dt->tm_hour, dt->tm_min, dt->tm_sec, ms, tzHrs < 0 ? "-" : "+", abs(tzHrs), abs(tzMin));
  return this->_timeBuffer;
}
int Timestamp::calcTZOffset(time_t *dt) {
  tm tmLocal, tmUTC;
  gmtime_r(dt, &tmUTC);
  localtime_r(dt, &tmLocal);
  long diff = mktime(&tmLocal) - mktime(&tmUTC);
  if(tmLocal.tm_isdst) diff += 3600;
  // Encoded as hhmm, e.g. +05:30 is 530 and -03:30 is -330.
  int hrs = (int)((diff/3600) * 100);
  int mins = (int)((diff % 3600) / 60);
  return hrs + mins;
}
int Timestamp::tzOffset() {
  time_t now;
  time(&now);
  return Timestamp::calcTZOffset(&now);
}
bool rssiNeedsEmit(int rssi, int lastRssi, int channel, int lastChannel, uint32_t sinceLastMs, int minDelta, uint32_t minMs) {
  if(channel != lastChannel) return true;
  int delta = rssi > lastRssi ? rssi - lastRssi : lastRssi - rssi;
  return delta >= minDelta && sinceLastMs >= minMs;
}
bool RssiBroadcast::due(int rssi, int channel, uint32_t now, int minDelta, uint32_t minMs) const {
  return rssiNeedsEmit(rssi, this->rssi, channel, this->channel, now - this->at, minDelta, minMs);
}
void RssiBroadcast::sent(bool broadcast, int rssi, int channel, uint32_t now) {
  if(!broadcast) return;
  this->rssi = rssi;
  this->channel = channel;
  this->at = now;
}
bool worthRoaming(bool connected, int currentRssi, int apRssi, int minGain) {
  return !connected || apRssi >= currentRssi + minGain;
}
heap_emit_t heapEmitTarget(uint8_t num, uint32_t sinceLastMs, uint32_t intervalMs, bool roomHasClients) {
  if(num != 255) return heap_emit_t::client;
  if(sinceLastMs >= intervalMs) return heap_emit_t::broadcast;
  return roomHasClients ? heap_emit_t::room : heap_emit_t::none;
}
void LoopStats::record(uint32_t passUs) {
  _passes++;
  if(passUs > _maxUs) _maxUs = passUs;
}
uint32_t LoopStats::rate(uint32_t nowMs) const {
  // The unsigned subtraction keeps the window right across a millis() wrap.
  const uint32_t elapsedMs = nowMs - _startMs;
  if(elapsedMs == 0) return 0;
  return (uint32_t)((uint64_t)_passes * 1000 / elapsedMs);
}
void LoopStats::publish(uint32_t nowMs) {
  _publishedRate.store(this->rate(nowMs), std::memory_order_relaxed);
  _publishedMaxUs.store(_maxUs, std::memory_order_relaxed);
  _passes = 0;
  _maxUs = 0;
  _startMs = nowMs;
}
