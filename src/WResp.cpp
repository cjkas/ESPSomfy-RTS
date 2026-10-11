#include "WResp.h"
#include "esp_log.h"

static const char *TAG = "WResp";
void JsonSockEvent::beginEvent(AsyncWebSocket *server, const char *evt, char *buff, size_t buffSize) {
  this->server = server;
  this->buff = buff;
  this->buffSize = buffSize;
  this->_nocomma = true;
  this->_closed = false;
  snprintf(this->buff, buffSize, "42[%s,", evt);
}
void JsonSockEvent::closeEvent() {
  if(!this->_closed) {
    if(strlen(this->buff) < buffSize) strcat(this->buff, "]");
    else this->buff[buffSize - 1] = ']';
  }
  this->_nocomma = true;
  this->_closed = true;
}
void JsonSockEvent::endEvent(uint32_t clientId) {
  this->closeEvent();
  if(clientId == 0) this->server->textAll(this->buff);
  else this->server->text(clientId, this->buff);
}
void JsonSockEvent::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    ESP_LOGE(TAG, "Socket exceeded buffer size %d - %d: %s", this->buffSize, len, this->buff);
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
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    this->flush();
  }
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}
