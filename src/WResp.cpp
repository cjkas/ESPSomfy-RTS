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
  snprintf(this->buff, buffSize, "42[%s,", evt);
}
void JsonSockEvent::closeEvent() {
  if(!this->_closed) {
    // The ] and the terminator both have to fit.
    if(strlen(this->buff) + 1 < buffSize) strcat(this->buff, "]");
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
  if(clientId == 0) snprintf(what, sizeof(what), "Socket emit %s (%u bytes) to all", this->_evt, (unsigned)strlen(this->buff));
  else snprintf(what, sizeof(what), "Socket emit %s (%u bytes) to %lu", this->_evt, (unsigned)strlen(this->buff), (unsigned long)clientId);
  logElapsed(TAG, what, us);
}
void JsonSockEvent::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    this->_overflow = true;
    return;
  }
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}
void AsyncJsonResp::beginResponse(AsyncWebServerRequest *request, char *buff, size_t buffSize) {
  this->_request = request;
  this->buff = buff;
  this->buffSize = buffSize;
  this->buff[0] = 0x00;
  this->_nocomma = true;
  this->_headersSent = false;
  this->_overflow = false;
  this->_stream = request->beginResponseStream("application/json");
}
void AsyncJsonResp::endResponse() {
  if(strlen(this->buff)) this->flush();
  if(this->_request && this->_stream) {
    this->_request->send(this->_stream);
  }
}
void AsyncJsonResp::flush() {
  if(this->_stream && strlen(this->buff) > 0) {
    this->_stream->print(this->buff);
    this->buff[0] = 0x00;
  }
}
void AsyncJsonResp::_safecat(const char *val, bool escape) {
  const size_t vlen = escape ? this->calcEscapedLength(val) + 2 : strlen(val);
  if(vlen + strlen(this->buff) >= this->buffSize) {
    this->flush();
    if(vlen + strlen(this->buff) >= this->buffSize) {
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
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}
