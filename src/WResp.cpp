#include "WResp.h"
#include "esp_log.h"
#include "Timing.h"

static const char *TAG = "WResp";
#define EMIT_SLOW_MS 20
void JsonSockEvent::beginEvent(AsyncWebSocket *server, const char *evt, char *buff, size_t buffSize) {
  this->server = server;
  this->_evt = evt;
  this->buff = buff;
  this->buffSize = buffSize;
  this->_nocomma = true;
  this->_closed = false;
  this->_overflow = false;
  const int n = snprintf(this->buff, buffSize, "42[%s,", evt);
  this->_len = n < 0 ? 0 : ((size_t)n < buffSize ? (size_t)n : buffSize - 1);
}
void JsonSockEvent::closeEvent() {
  if(!this->_closed) {
    // The ] and the terminator both have to fit.
    if(this->_len + 1 < buffSize) this->_appendAt("]", false, 1);
    else this->_overflow = true;
  }
  this->_nocomma = true;
  this->_closed = true;
}
void JsonSockEvent::endEvent(uint32_t clientId) {
  this->closeEvent();
  // Part of it is missing so clients would only fail to parse it.
  if(this->_overflow) {
    ESP_LOGE(TAG, "Socket event too large for %u bytes, not sent: %.32s", (unsigned)this->buffSize, this->buff);
    return;
  }
  ElapsedTimer timer;
  if(clientId == 0) this->server->textAll(this->buff);
  else this->server->text(clientId, this->buff);
  // Sends happen every few seconds, so only the slow ones are worth a line.
  const uint32_t us = timer.us();
  if(us < EMIT_SLOW_MS * 1000) return;
  char what[80];
  if(clientId == 0) snprintf(what, sizeof(what), "Socket emit %s (%u bytes) to all", this->_evt, (unsigned)this->_len);
  else snprintf(what, sizeof(what), "Socket emit %s (%u bytes) to %lu", this->_evt, (unsigned)this->_len, (unsigned long)clientId);
  logElapsed(TAG, what, us);
}
void JsonSockEvent::_safecat(const char *val, bool escape) {
  const size_t vlen = escape ? this->calcEscapedLength(val) + 2 : strlen(val);
  if(this->_len + vlen >= this->buffSize) {
    this->_overflow = true;
    return;
  }
  this->_appendAt(val, escape, vlen);
}
void AsyncJsonResp::beginResponse(AsyncWebServerRequest *request, char *buff, size_t buffSize) {
  this->_request = request;
  this->buff = buff;
  this->buffSize = buffSize;
  this->buff[0] = 0x00;
  this->_len = 0;
  this->_nocomma = true;
  this->_headersSent = false;
  this->_overflow = false;
  this->_stream = request->beginResponseStream("application/json");
}
void AsyncJsonResp::endResponse() {
  if(this->_len) this->flush();
  if(this->_request && this->_stream) {
    this->_request->send(this->_stream);
  }
}
void AsyncJsonResp::flush() {
  if(this->_stream && this->_len > 0) {
    this->_stream->write((const uint8_t *)this->buff, this->_len);
    this->buff[0] = 0x00;
    this->_len = 0;
  }
}
void AsyncJsonResp::_safecat(const char *val, bool escape) {
  const size_t vlen = escape ? this->calcEscapedLength(val) + 2 : strlen(val);
  if(vlen + this->_len >= this->buffSize) {
    this->flush();
    if(vlen + this->_len >= this->buffSize) {
      // Bigger than the whole buffer so stream it straight out.
      if(!this->_stream) {
        this->_overflow = true;
        return;
      }
      if(!escape) {
        this->_stream->print(val);
        return;
      }
      char esc[7];
      this->_stream->print("\"");
      for(size_t i = 0; val[i]; i++) {
        escapeChar(val[i], esc);
        this->_stream->print(esc);
      }
      this->_stream->print("\"");
      return;
    }
  }
  this->_appendAt(val, escape, vlen);
}
