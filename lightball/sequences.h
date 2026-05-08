#pragma once
// Embedded sequences — used only when USE_EMBEDDED_SEQ is defined.
// Transcribed verbatim from sd_card/led0.txt and sd_card/led1.txt.
// Note: durations >65535 narrow to uint16_t (matches the SD-mode parser's
// existing truncation behavior). Long clips should be split at export time.

#include <avr/pgmspace.h>
#include "types.h"

const LEDCommand SEQ_LED0[] PROGMEM = {
  {CMD_COLOR, 2000, 0, 0, 255,  59,  59, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0, 255, 201,  51, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_WAIT,     0, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_LOOP,     0, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_COLOR, (uint16_t)227500, 0, 0, 0, 0, 0, 0, 0, 0},
  {CMD_LOOP,     0, 0, 0,   0,   0,   0, 0, 0, 0},
};

const LEDCommand SEQ_LED1[] PROGMEM = {
  {CMD_COLOR, 2000, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0, 255, 138,   0, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0,   0,   0,   0, 0, 0, 0},
  {CMD_COLOR, 2000, 0, 0,  61, 220, 132, 0, 0, 0},
  {CMD_COLOR, (uint16_t)228000, 0, 0, 0, 0, 0, 0, 0, 0},
  {CMD_LOOP,     0, 0, 0,   0,   0,   0, 0, 0, 0},
};
