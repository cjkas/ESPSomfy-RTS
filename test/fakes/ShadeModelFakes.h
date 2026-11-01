// Recording stand-ins for the shade model's ports, shared by the suites that
// run the real model (pio test -e native).
#ifndef shade_model_fakes_h
#define shade_model_fakes_h
#include <string>
#include <vector>
#include "model/ShadeModel.h"

// One job the model handed the radio.  frame is decoded from the encoded
// payload, so it is what went on air.
struct RadioJob {
  somfy_frame_t frame;
  uint8_t repeats;
  bool priority;
  uint16_t delayMs;
  uint32_t ticket;
};
struct Pulse {
  uint8_t shadeId;
  somfy_commands cmd;
};
// Hands out tickets 1, 2, 3, ...  refuseNext makes the radio refuse the next
// job.  Refused jobs aren't recorded.
struct RecordingRadio : RadioPort {
  std::vector<RadioJob> jobs;
  std::vector<Pulse> pulses;
  uint32_t nextTicket = 1;
  bool refuseNext = false;
  uint32_t submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs) override {
    if(this->refuseNext) {
      this->refuseNext = false;
      return 0;
    }
    RadioJob j;
    j.frame.bitLength = job.tx.bit_length;
    j.frame.decodeFrame(job.tx.payload);
    j.repeats = job.repeats;
    j.priority = priority;
    j.delayMs = delayMs;
    j.ticket = this->nextTicket++;
    this->jobs.push_back(j);
    return j.ticket;
  }
  void pulse(SomfyShade &shade, const somfy_frame_t &frame) override { this->pulses.push_back({shade.getShadeId(), frame.cmd}); }
  const RadioJob &last() const { return this->jobs.back(); }
};
struct ModelCommand {
  uint8_t shadeId;
  somfy_commands cmd;
  std::string source;
  uint32_t sourceAddress;
};
struct RecordingEvents : ModelEvents {
  std::vector<ModelCommand> commands;
  void onCommand(SomfyShade &shade, somfy_commands cmd, const char *source, uint32_t sourceAddress) override {
    this->commands.push_back({shade.getShadeId(), cmd, source, sourceAddress});
  }
};
#endif
