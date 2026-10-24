#ifndef PROTO_ASSEMBLY_H
#define PROTO_ASSEMBLY_H
#include <stddef.h>
#include <stdint.h>

// Putting a protocol 2 message back together from the pieces the web server
// hands over: a frame larger than a TCP segment, or a message sent as several
// frames (docs/PROTOCOL.md).  One message at a time across every client, in
// one buffer taken when its first piece arrives and freed when it is whole, so
// nothing is held while no long message is on its way.  async_tcp only.  Kept
// free of the web server so it can be unit tested on the PC
// (pio test -e native).

// How long a message may take to arrive in full before another client's may
// take the buffer.
#define PROTO_ASSEMBLY_TIMEOUT_MS 10000

// One piece of a message.
struct proto_piece_t {
  uint32_t clientId = 0;
  bool first = false;  // its first piece: the start of its first frame
  bool last = false;   // its last piece: the end of its final frame
  bool text = true;    // a text message rather than binary
  const uint8_t *data = nullptr;
  size_t len = 0;
};
enum class assembly_t : uint8_t {
  more,       // wait for the next piece
  complete,   // the message is whole: text() and length(), then done()
  ignored,    // a piece of a message that was already answered
  busy,       // another client's message holds the buffer
  tooLarge,   // over the limit: the rest of it is ignored
  notText,    // a binary message: the rest of it is ignored
  noMemory,   // the buffer couldn't be had: the rest of it is ignored
  timedOut    // it stalled and another client's took the buffer
};
class MessageAssembler {
  public:
    explicit MessageAssembler(size_t limit) : limit(limit) {}
    ~MessageAssembler() { this->free(); }
    // Each reply-worthy result (busy, tooLarge, notText, noMemory, timedOut)
    // comes once per message, from the piece that caused it.
    assembly_t add(const proto_piece_t &piece, uint32_t now);
    // The whole message, terminated, until done().
    const char *text() const { return this->buf; }
    size_t length() const { return this->len; }
    void done() { this->free(); }
    // The client has gone: its part of a message goes with it.
    void drop(uint32_t clientId);
    bool holding() const { return this->owner != 0; }
  private:
    size_t limit;
    uint32_t owner = 0;
    uint32_t startedAt = 0;
    // The client whose stalled message lost the buffer, still to be told.
    uint32_t stalled = 0;
    char *buf = nullptr;
    size_t len = 0;
    void free();
};
#endif
