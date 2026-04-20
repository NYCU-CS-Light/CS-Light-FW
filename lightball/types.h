#pragma once
#include <SD.h>

struct LEDPins { uint8_t r, g, b; };

enum CmdType : uint8_t {
  CMD_COLOR    = 0,
  CMD_BLINK    = 1,
  CMD_FADE     = 2,
  CMD_BREATHE  = 3,
  CMD_PINGPONG = 4,
  CMD_WAIT     = 5,
  CMD_LOOP     = 6
};

struct LEDCommand {
  CmdType  type;
  uint16_t durationMs;
  uint16_t onMs;
  uint16_t offMs;
  uint8_t  r, g, b;
  uint8_t  r2, g2, b2;
};

struct LEDSeqState {
  uint8_t       ledId;
  const char*   filename;
  File          file;
  LEDCommand    cmd;              // currently playing
  LEDCommand    next;             // prefetched, ready for zero-wait swap
  bool          hasNext    = false;
  bool          active     = false;
  unsigned long cmdStartMs = 0;   // absolute time cmd started (drift-free)
  int16_t       loopsLeft  = -1;  // -1 = uninitialized, used by CMD_LOOP

  LEDSeqState(uint8_t id, const char* fn)
    : ledId(id), filename(fn), hasNext(false), active(false), cmdStartMs(0), loopsLeft(-1) {}
};
