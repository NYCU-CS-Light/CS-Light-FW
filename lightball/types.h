#pragma once
#ifndef USE_EMBEDDED_SEQ
#include <SdFat.h>
#endif

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
#ifdef USE_EMBEDDED_SEQ
  const LEDCommand* seqData;        // PROGMEM array
  uint16_t          seqLen;
  uint16_t          seqIdx;
#else
  const char*   filename;
  FsFile        file;
#endif
  LEDCommand    cmd;              // currently playing
  LEDCommand    next;             // prefetched, ready for zero-wait swap
  bool          hasNext    = false;
  bool          active     = false;
  unsigned long cmdStartMs = 0;   // absolute time cmd started (drift-free)
  int16_t       loopsLeft  = -1;  // -1 = uninitialized, used by CMD_LOOP

#ifdef USE_EMBEDDED_SEQ
  LEDSeqState(uint8_t id, const LEDCommand* data, uint16_t len)
    : ledId(id), seqData(data), seqLen(len), seqIdx(0),
      hasNext(false), active(false), cmdStartMs(0), loopsLeft(-1) {}
#else
  LEDSeqState(uint8_t id, const char* fn)
    : ledId(id), filename(fn), hasNext(false), active(false), cmdStartMs(0), loopsLeft(-1) {}
#endif
};
