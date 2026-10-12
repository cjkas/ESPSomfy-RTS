#include "RadioScheduler.h"
#include "esp_log.h"

// Logged as part of the radio code it came from.
static const char *TAG = "Somfy";

#define TX_EVENT_QUEUE_SIZE (TX_JOB_QUEUE_SIZE * 3)

// Whether a job fits with spaces slots free.  Normal jobs leave the reserved
// slots free for stops.
static bool admits(uint32_t spaces, bool priority) {
  return spaces > (priority ? 0u : (uint32_t)TX_RESERVED_SLOTS);
}
uint32_t RadioScheduler::submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs, uint32_t now) {
  job.seq = 0;
  if(this->suspendMask) {
    ESP_LOGW(TAG, "Radio TX suspended, dropping transmission");
    return 0;
  }
  if(!admits(TX_JOB_QUEUE_SIZE - this->count, priority)) {
    ESP_LOGE(TAG, "Radio TX queue full, dropping transmission");
    return 0;
  }
  // 0 means no job so it is skipped on wrap.
  if(++this->lastSeq == 0) ++this->lastSeq;
  job.seq = this->lastSeq;
  job.priority = priority;
  job.front = priority && !job.raw && !this->waitingFor(job.frame.remoteAddress);
  job.notBefore = 0;
  if(delayMs > 0) {
    job.notBefore = now + delayMs;
    if(job.notBefore == 0) job.notBefore = 1;  // 0 means not delayed.
  }
  // Front jobs go out in the order they came, ahead of the back.
  uint8_t at = this->count;
  if(job.front) {
    at = 0;
    while(at < this->count && this->jobs[at].front) at++;
  }
  for(uint8_t i = this->count; i > at; i--) this->jobs[i] = this->jobs[i - 1];
  this->jobs[at] = job;
  this->count++;
  return job.seq;
}
bool RadioScheduler::hasRoom(bool priority) const {
  return !this->suspendMask && admits(TX_JOB_QUEUE_SIZE - this->count, priority);
}
bool RadioScheduler::waitingFor(uint32_t remoteAddress) const {
  for(uint8_t i = 0; i < this->count; i++) {
    if(!this->jobs[i].raw && this->jobs[i].frame.remoteAddress == remoteAddress) return true;
  }
  return false;
}
void RadioScheduler::remove(uint8_t i) {
  for(; i + 1 < this->count; i++) this->jobs[i] = this->jobs[i + 1];
  this->count--;
}
bool RadioScheduler::next(uint32_t now, somfy_tx_job_t &job, uint32_t &waitMs) {
  waitMs = RADIO_WAIT_FOREVER;
  if(this->count == 0) return false;
  // Jobs go out in order, so a delayed job holds up the ones behind it.  A
  // front job queued meanwhile is ahead of it.
  const somfy_tx_job_t &head = this->jobs[0];
  if(head.notBefore != 0) {
    // Works across a millis() wrap.
    const int32_t due = (int32_t)(head.notBefore - now);
    if(due > 0) {
      waitMs = (uint32_t)due;
      return false;
    }
  }
  job = head;
  this->remove(0);
  this->taken = job.seq;
  this->takenFlushed = false;
  this->takenJob = job;
  return true;
}
bool RadioScheduler::start(uint32_t ticket, uint32_t now) {
  if(ticket == 0 || ticket != this->taken) return false;
  if(this->takenFlushed) {
    // Flushed after the radio task took it.
    this->post(tx_event_t::dropped, this->takenJob, now);
    this->taken = 0;
    return false;
  }
  this->post(tx_event_t::started, this->takenJob, now);
  return true;
}
bool RadioScheduler::keepSending(uint32_t ticket) const {
  return ticket != 0 && ticket == this->taken && !this->takenFlushed;
}
void RadioScheduler::ended(uint32_t ticket, bool complete, uint32_t now) {
  if(ticket == 0 || ticket != this->taken) return;
  this->post(complete ? tx_event_t::finished : tx_event_t::aborted, this->takenJob, now);
  this->taken = 0;
}
void RadioScheduler::flush(uint32_t now) {
  // Every discarded job is reported as dropped.
  for(uint8_t i = 0; i < this->count; i++) this->post(tx_event_t::dropped, this->jobs[i], now);
  this->count = 0;
  if(this->taken) this->takenFlushed = true;
}
void RadioScheduler::suspend(uint8_t reason, uint32_t now) {
  this->suspendMask |= reason;
  this->flush(now);
}
void RadioScheduler::resume(uint8_t reason) { this->suspendMask &= ~reason; }
void RadioScheduler::post(tx_event_t type, const somfy_tx_job_t &job, uint32_t at) {
  // Repeater frames don't belong to any of our remotes.
  if(job.raw) return;
  if(this->eventCount >= TX_EVENT_QUEUE_SIZE) {
    ESP_LOGW(TAG, "Radio TX event queue full");
    return;
  }
  somfy_tx_event_t &evt = this->events[(this->eventHead + this->eventCount) % TX_EVENT_QUEUE_SIZE];
  evt.type = type;
  evt.cmd = job.frame.cmd;
  evt.remoteAddress = job.frame.remoteAddress;
  evt.seq = job.seq;
  evt.at = at;
  this->eventCount++;
}
bool RadioScheduler::popEvent(somfy_tx_event_t &evt) {
  if(this->eventCount == 0) return false;
  evt = this->events[this->eventHead];
  this->eventHead = (uint8_t)((this->eventHead + 1) % TX_EVENT_QUEUE_SIZE);
  this->eventCount--;
  return true;
}
