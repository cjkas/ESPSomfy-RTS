#include <stdlib.h>
#include <string.h>
#include "ProtoAssembly.h"

void MessageAssembler::free() {
  ::free(this->buf);
  this->buf = nullptr;
  this->len = 0;
  this->owner = 0;
}
void MessageAssembler::drop(uint32_t clientId) {
  if(this->owner == clientId) this->free();
  if(this->stalled == clientId) this->stalled = 0;
}
assembly_t MessageAssembler::add(const proto_piece_t &piece, uint32_t now) {
  if(piece.first) {
    if(this->stalled == piece.clientId) this->stalled = 0;
    if(this->owner && this->owner != piece.clientId) {
      if(now - this->startedAt < PROTO_ASSEMBLY_TIMEOUT_MS) return assembly_t::busy;
      // A message that stalled gives the buffer up after a while.
      this->stalled = this->owner;
    }
    this->free();
    if(!piece.text) return assembly_t::notText;
    this->buf = static_cast<char *>(malloc(this->limit + 1));
    if(!this->buf) return assembly_t::noMemory;
    this->owner = piece.clientId;
    this->startedAt = now;
  }
  else if(this->owner != piece.clientId) {
    if(this->stalled == piece.clientId) {
      this->stalled = 0;
      return assembly_t::timedOut;
    }
    // The rest of a message that was already answered.
    return assembly_t::ignored;
  }
  if(this->len + piece.len > this->limit) {
    this->free();
    return assembly_t::tooLarge;
  }
  memcpy(this->buf + this->len, piece.data, piece.len);
  this->len += piece.len;
  if(!piece.last) return assembly_t::more;
  this->buf[this->len] = '\0';
  return assembly_t::complete;
}
