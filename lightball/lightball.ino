// Uncomment to bake sequences into firmware (no SD card required).
// When defined, SEQ_LED0/SEQ_LED1 from sequences.h replace led0.txt/led1.txt.
#define USE_EMBEDDED_SEQ

#ifndef USE_EMBEDDED_SEQ
  #include <SPI.h>
  #include <SdFat.h>
#endif
#include <math.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "types.h"
#include "calibration.h"
#ifdef USE_EMBEDDED_SEQ
  #include "sequences.h"
#endif
#include "btnState.h"
#ifdef USE_EMBEDDED_SEQ
  #include "sequences.h"
#endif

#ifndef USE_EMBEDDED_SEQ
SdFs sd;
#endif

#ifndef M_PI
#define M_PI 3.14159265f
#endif

// Set to 1 to log every command transition during playback. 0 = zero overhead.
#define DEBUG_SEQ 0

#if DEBUG_SEQ
  #define DBG_LOG_CUR(st)  logCmd((st).ledId, (st).cmd)
#else
  #define DBG_LOG_CUR(st)  do { } while (0)
#endif

// ---------------------- User Config ----------------------
const int BTN_PIN   = A4;
const int SD_CS_PIN = 8;

// ---------------------- LED Pins -------------------------
const LEDPins LED_PINS[2] = { {9, 6, 11}, {13, 5, 10} };

void setLED(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
  // LUTs live in flash via PROGMEM; pgm_read_byte fetches them across the
  // Harvard bus on AVR. Required — putting 768 B of LUT in SRAM exhausts the
  // Uno's 2 KB and wedges the program (button reads stop working, SD reads
  // misbehave, etc).
  analogWrite(LED_PINS[id].r, pgm_read_byte(&CAL_LUT_R[r]));
  analogWrite(LED_PINS[id].g, pgm_read_byte(&CAL_LUT_G[g]));
  analogWrite(LED_PINS[id].b, pgm_read_byte(&CAL_LUT_B[b]));
}

void haltWithBlink(uint8_t r, uint8_t g, uint8_t b) {
  while (1) {
    setLED(0, r, g, b); setLED(1, r, g, b); delay(200);
    setLED(0, 0, 0, 0); setLED(1, 0, 0, 0); delay(200);
  }
}

// ---------------------- Runtime State --------------------
#ifdef USE_EMBEDDED_SEQ
LEDSeqState s0(0, SEQ_LED0, sizeof(SEQ_LED0) / sizeof(LEDCommand));
LEDSeqState s1(1, SEQ_LED1, sizeof(SEQ_LED1) / sizeof(LEDCommand));
#else
LEDSeqState s0(0, "led0.txt");
LEDSeqState s1(1, "led1.txt");
#endif

// Shared button edge flag — set in onBtnSingle(), consumed by CMD_WAIT in stepSeq
static bool g_buttonEdge = false;
static bool g_canStart   = false;

btnState btn(BTN_PIN);

// ---------------------- SD Helpers -----------------------
#ifndef USE_EMBEDDED_SEQ
// Format: type, duration, on, off, r, g, b, r2, g2, b2
bool parseCommandLine(char* buf, LEDCommand &cmd) {
  int len = strlen(buf);
  while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == ' ')) buf[--len] = '\0';
  if (len == 0 || buf[0] == '#') return false;

  int vals[10];
  int idx = 0;
  char* p = strtok(buf, ",");
  while (p && idx < 10) {
    while (*p == ' ') p++;
    vals[idx++] = atoi(p);
    p = strtok(NULL, ",");
  }
  if (idx != 10) return false;

  cmd.type       = (CmdType)(uint8_t)vals[0];
  cmd.durationMs = (uint16_t)vals[1];
  cmd.onMs       = (uint16_t)vals[2];
  cmd.offMs      = (uint16_t)vals[3];
  cmd.r          = (uint8_t)vals[4];
  cmd.g          = (uint8_t)vals[5];
  cmd.b          = (uint8_t)vals[6];
  cmd.r2         = (uint8_t)vals[7];
  cmd.g2         = (uint8_t)vals[8];
  cmd.b2         = (uint8_t)vals[9];
  return true;
}
#endif // !USE_EMBEDDED_SEQ

#if DEBUG_SEQ
const char* cmdName(CmdType t) {
  switch (t) {
    case CMD_COLOR:    return "COLOR";
    case CMD_BLINK:    return "BLINK";
    case CMD_FADE:     return "FADE";
    case CMD_BREATHE:  return "BREATHE";
    case CMD_PINGPONG: return "PINGPONG";
    case CMD_WAIT:     return "WAIT";
    case CMD_LOOP:     return "LOOP";
    default:           return "?";
  }
}

void logCmd(uint8_t ledId, const LEDCommand& cmd) {
  Serial.print("[LED"); Serial.print(ledId); Serial.print("] ");
  Serial.print(cmdName(cmd.type));
  Serial.print(" dur="); Serial.print(cmd.durationMs);
  switch (cmd.type) {
    case CMD_COLOR:
      Serial.print(" rgb=("); Serial.print(cmd.r); Serial.print(",");
      Serial.print(cmd.g); Serial.print(","); Serial.print(cmd.b); Serial.print(")");
      break;
    case CMD_BLINK:
      Serial.print(" on="); Serial.print(cmd.onMs);
      Serial.print(" off="); Serial.print(cmd.offMs);
      Serial.print(" a=("); Serial.print(cmd.r); Serial.print(",");
      Serial.print(cmd.g); Serial.print(","); Serial.print(cmd.b);
      Serial.print(") b=("); Serial.print(cmd.r2); Serial.print(",");
      Serial.print(cmd.g2); Serial.print(","); Serial.print(cmd.b2); Serial.print(")");
      break;
    case CMD_FADE:
    case CMD_PINGPONG:
      Serial.print(" a=("); Serial.print(cmd.r); Serial.print(",");
      Serial.print(cmd.g); Serial.print(","); Serial.print(cmd.b);
      Serial.print(")->b=("); Serial.print(cmd.r2); Serial.print(",");
      Serial.print(cmd.g2); Serial.print(","); Serial.print(cmd.b2); Serial.print(")");
      if (cmd.type == CMD_PINGPONG) { Serial.print(" period="); Serial.print(cmd.onMs); }
      break;
    case CMD_BREATHE:
      Serial.print(" period="); Serial.print(cmd.onMs);
      Serial.print(" rgb=("); Serial.print(cmd.r); Serial.print(",");
      Serial.print(cmd.g); Serial.print(","); Serial.print(cmd.b); Serial.print(")");
      break;
    case CMD_LOOP:
      if (cmd.durationMs == 0) Serial.print(" infinite");
      else { Serial.print(" count="); Serial.print(cmd.durationMs); }
      break;
    default: break;
  }
  Serial.println();
}
#endif // DEBUG_SEQ

// Pure source read — no state mutation. Returns true if a cmd was loaded.
bool readNextCmd(LEDSeqState &st, LEDCommand &out) {
#ifdef USE_EMBEDDED_SEQ
  if (st.seqIdx >= st.seqLen) return false;
  memcpy_P(&out, &st.seqData[st.seqIdx], sizeof(LEDCommand));
  st.seqIdx++;
  return true;
#else
  char buf[64];
  while (st.file.available()) {
    int len = st.file.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[len] = '\0';
    if (parseCommandLine(buf, out)) return true;
  }
  return false;
#endif
}

// Fill the prefetch slot if empty. Safe to call every frame.
void tryPrefetchNext(LEDSeqState &st) {
  if (!st.active || st.hasNext) return;
#ifndef USE_EMBEDDED_SEQ
  if (!st.file) return;
#endif
  if (readNextCmd(st, st.next)) st.hasNext = true;
  // else: EOF — swap time will mark sequence inactive.
}

// Move next -> cmd. Sync fallback on prefetch miss. Returns false on EOF.
bool swapInNext(LEDSeqState &st) {
  if (st.hasNext) {
    st.cmd     = st.next;
    st.hasNext = false;
    return true;
  }
  LEDCommand tmp;
  if (readNextCmd(st, tmp)) { st.cmd = tmp; return true; }
#ifndef USE_EMBEDDED_SEQ
  st.file.close();
#endif
  st.active = false;
#if DEBUG_SEQ
  Serial.print("[LED"); Serial.print(st.ledId); Serial.println("] END");
#endif
  return false;
}

// Drift-free advance: new cmdStartMs = old cmdStartMs + OLD cmd.durationMs.
bool advanceAligned(LEDSeqState &st) {
  uint16_t oldDur = st.cmd.durationMs;
  if (!swapInNext(st)) return false;
  st.cmdStartMs += oldDur;
  DBG_LOG_CUR(st);
  return true;
}

// Re-anchored advance: cmdStartMs = newStart (for WAIT / reset-to-now cases).
bool advanceAnchored(LEDSeqState &st, unsigned long newStart) {
  if (!swapInNext(st)) return false;
  st.cmdStartMs = newStart;
  DBG_LOG_CUR(st);
  return true;
}

// ---------------------- Sequence Runtime -----------------
void stopSeq(LEDSeqState &st) {
  if (st.file) st.file.close();
  st.active  = false;
  st.hasNext = false;
}

// baseNow is shared across both LEDs so they share a single timeline origin.
void startSeq(LEDSeqState &st, unsigned long baseNow) {
#ifdef USE_EMBEDDED_SEQ
  st.seqIdx = 0;
#else
  if (st.file) st.file.close();
  st.file = sd.open(st.filename, O_RDONLY);
  if (!st.file) {
    Serial.print("Failed to open "); Serial.println(st.filename);
    st.active = false;
    return;
  }
#endif
  st.loopsLeft = -1;
  st.hasNext   = false;
  LEDCommand first;
  if (!readNextCmd(st, first)) {
#ifndef USE_EMBEDDED_SEQ
    st.file.close();
#endif
    st.active = false;
    return;
  }
  st.cmd        = first;
  st.cmdStartMs = baseNow;
  st.active     = true;
  DBG_LOG_CUR(st);
  tryPrefetchNext(st);  // eager: have next ready before first transition
}

bool stepSeq(LEDSeqState &st, unsigned long now) {
  if (!st.active) return false;

  uint8_t       ledId   = st.ledId;
  const LEDCommand &cmd = st.cmd;
  unsigned long elapsed = now - st.cmdStartMs;

  switch (cmd.type) {

    case CMD_COLOR: {
      setLED(ledId, cmd.r, cmd.g, cmd.b);
      if (cmd.durationMs == 0 || elapsed >= cmd.durationMs)
        advanceAligned(st);
    } break;

    case CMD_BLINK: {
      if (elapsed >= cmd.durationMs) { advanceAligned(st); break; }
      uint32_t cycle = (uint32_t)cmd.onMs + (uint32_t)cmd.offMs;
      if (cycle == 0) { setLED(ledId, cmd.r, cmd.g, cmd.b); break; }
      if ((elapsed % cycle) < cmd.onMs)
        setLED(ledId, cmd.r,  cmd.g,  cmd.b);
      else
        setLED(ledId, cmd.r2, cmd.g2, cmd.b2);
    } break;

    case CMD_FADE: {
      if (cmd.durationMs == 0) {
        setLED(ledId, cmd.r2, cmd.g2, cmd.b2);
        advanceAligned(st);
        break;
      }
      float a = (elapsed >= cmd.durationMs) ? 1.0f : (float)elapsed / (float)cmd.durationMs;
      setLED(ledId,
        (uint8_t)((1.0f - a) * cmd.r  + a * cmd.r2),
        (uint8_t)((1.0f - a) * cmd.g  + a * cmd.g2),
        (uint8_t)((1.0f - a) * cmd.b  + a * cmd.b2));
      if (elapsed >= cmd.durationMs) advanceAligned(st);
    } break;

    case CMD_BREATHE: {
      if (elapsed >= cmd.durationMs) { advanceAligned(st); break; }
      if (cmd.onMs == 0) { setLED(ledId, cmd.r, cmd.g, cmd.b); break; }
      float t = (float)(elapsed % cmd.onMs) / (float)cmd.onMs;
      float a = (1.0f - cosf(2.0f * (float)M_PI * t)) * 0.5f;
      setLED(ledId,
        (uint8_t)(a * cmd.r),
        (uint8_t)(a * cmd.g),
        (uint8_t)(a * cmd.b));
    } break;

    case CMD_PINGPONG: {
      if (elapsed >= cmd.durationMs) { advanceAligned(st); break; }
      if (cmd.onMs == 0) { setLED(ledId, cmd.r, cmd.g, cmd.b); break; }
      float t = (float)(elapsed % cmd.onMs) / (float)cmd.onMs;
      float a = (1.0f - cosf(2.0f * (float)M_PI * t)) * 0.5f;
      setLED(ledId,
        (uint8_t)((1.0f - a) * cmd.r  + a * cmd.r2),
        (uint8_t)((1.0f - a) * cmd.g  + a * cmd.g2),
        (uint8_t)((1.0f - a) * cmd.b  + a * cmd.b2));
    } break;

    case CMD_WAIT: {
      if (g_buttonEdge) {
#if DEBUG_SEQ
        Serial.print("[LED"); Serial.print(ledId); Serial.println("] WAIT -> btn");
#endif
        advanceAnchored(st, now);  // re-anchor timeline to button press
      }
    } break;

    case CMD_LOOP: {
      if (cmd.durationMs == 0) {
        // infinite loop
#if DEBUG_SEQ
        Serial.print("[LED"); Serial.print(ledId); Serial.println("] LOOP rewind");
#endif
#ifdef USE_EMBEDDED_SEQ
        st.seqIdx = 0;
#else
        st.file.seekSet(0);
#endif
        st.hasNext = false;
        tryPrefetchNext(st);
        advanceAligned(st);  // LOOP.durationMs = 0, so timeline is preserved
      } else {
        if (st.loopsLeft < 0) st.loopsLeft = (int16_t)(cmd.durationMs - 1);
        if (st.loopsLeft > 0) {
          st.loopsLeft--;
#if DEBUG_SEQ
          Serial.print("[LED"); Serial.print(ledId);
          Serial.print("] LOOP rewind left="); Serial.println(st.loopsLeft);
#endif
#ifdef USE_EMBEDDED_SEQ
          st.seqIdx = 0;
#else
          st.file.seekSet(0);
#endif
          st.hasNext = false;
          tryPrefetchNext(st);
          advanceAligned(st);
        } else {
#if DEBUG_SEQ
          Serial.print("[LED"); Serial.print(ledId); Serial.println("] LOOP done");
#endif
          st.loopsLeft = -1;
#ifndef USE_EMBEDDED_SEQ
          st.file.close();
#endif
          st.active = false;
        }
      }
    } break;

    default:
      advanceAligned(st);
      break;
  }

  return st.active;
}

// ---------------------- Serial File Commands ------------
// Update SD txt files over Serial without a card reader.
// Commands (send when sequence is idle):
//   LIST             - list files on SD
//   CAT <file>       - print file contents
//   WRITE <file>     - overwrite file; then send lines, finish with "END"
//   DEL <file>       - delete file
//   HELP             - show this list
char    g_serialBuf[96];
uint8_t g_serialIdx = 0;

// Line terminators: '\n', '\r', ';'
// ';' lets you paste multi-line content as one line (Arduino IDE Serial Monitor's
// input field strips newlines on paste — replace '\n' with ';' first).
bool readSerialLine() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ';') {
      if (g_serialIdx == 0) continue;  // skip empty (handles \r\n or ;;)
      g_serialBuf[g_serialIdx] = '\0';
      g_serialIdx = 0;
      return true;
    } else if (g_serialIdx < sizeof(g_serialBuf) - 1) {
      g_serialBuf[g_serialIdx++] = c;
    }
  }
  return false;
}

// void cmdList() {
//   FsFile root = sd.open("/");
//   FsFile e;
//   char name[64];
//   while (e.openNext(&root, O_RDONLY)) {
//     e.getName(name, sizeof(name));
//     Serial.print("  "); Serial.print(name);
//     Serial.print("  "); Serial.print((uint32_t)e.fileSize()); Serial.println(" B");
//     e.close();
//   }
//   root.close();
// }

// void cmdCat(const char* fn) {
//   FsFile f = sd.open(fn, O_RDONLY);
//   if (!f) { Serial.print("ERR: not found: "); Serial.println(fn); return; }
//   while (f.available()) Serial.write(f.read());
//   f.close();
//   Serial.println();
//   Serial.println("---END---");
// }

// void cmdWrite(const char* fn) {
//   if (sd.exists(fn)) sd.remove(fn);
//   FsFile f = sd.open(fn, O_WRONLY | O_CREAT | O_TRUNC);
//   if (!f) { Serial.print("ERR: cannot open: "); Serial.println(fn); return; }
//   Serial.print("READY "); Serial.println(fn);
//   Serial.println("(one line per send, OR use ';' as line separator; end with 'END')");
//   uint16_t lines = 0;
//   while (true) {
//     if (readSerialLine()) {
//       if (strcmp(g_serialBuf, "END") == 0) break;
//       f.println(g_serialBuf);
//       lines++;
//     }
//   }
//   f.close();
//   Serial.print("DONE "); Serial.print(lines); Serial.println(" lines");
// }

// void cmdDel(const char* fn) {
//   if (sd.remove(fn)) { Serial.print("Deleted "); Serial.println(fn); }
//   else               { Serial.print("ERR: cannot delete: "); Serial.println(fn); }
// }

// void cmdHelp() {
//   Serial.println(F("Commands:"));
//   Serial.println(F("  LIST            list files"));
//   Serial.println(F("  CAT <file>      print file"));
//   Serial.println(F("  WRITE <file>    overwrite file; send lines then 'END'"));
//   Serial.println(F("  DEL <file>      delete file"));
//   Serial.println(F("  HELP            this list"));
//   Serial.println(F("  (button press)  start sequence"));
// }

// void handleSerialCommand() {
//   if (!readSerialLine()) return;
//   if      (strncmp(g_serialBuf, "WRITE ", 6) == 0) cmdWrite(g_serialBuf + 6);
//   else if (strncmp(g_serialBuf, "CAT ",   4) == 0) cmdCat  (g_serialBuf + 4);
//   else if (strncmp(g_serialBuf, "DEL ",   4) == 0) cmdDel  (g_serialBuf + 4);
//   else if (strcmp (g_serialBuf, "LIST")      == 0) cmdList();
//   else if (strcmp (g_serialBuf, "HELP")      == 0) cmdHelp();
//   else if (g_serialBuf[0] != '\0') {
//     Serial.print("Unknown: '"); Serial.print(g_serialBuf); Serial.println("' (try HELP)");
//   }
// }

// ---------------------- Button Callbacks -----------------
void onBtnSingle() {
  g_buttonEdge = true;
  if (!g_canStart) {
    g_canStart = true;
    unsigned long baseNow = millis();
    startSeq(s0, baseNow);
    startSeq(s1, baseNow);
    Serial.println("Sequence start");
  }
}

void onBtnLong() {
  if (g_canStart) {
    stopSeq(s0);
    stopSeq(s1);
    setLED(0, 0, 0, 0);
    setLED(1, 0, 0, 0);
    g_canStart = false;
    Serial.println("Hold reset. Press button to restart.");
  }
}

// ---------------------- Setup ----------------------------
void setup() {
  Serial.begin(115200);
  { unsigned long t = millis(); while (!Serial && (millis() - t) < 3000) { ; } }
  Serial.println("boot");
  Serial.flush();

  for (int i = 0; i < 2; i++) {
    pinMode(LED_PINS[i].r, OUTPUT); digitalWrite(LED_PINS[i].r, LOW);
    pinMode(LED_PINS[i].g, OUTPUT); digitalWrite(LED_PINS[i].g, LOW);
    pinMode(LED_PINS[i].b, OUTPUT); digitalWrite(LED_PINS[i].b, LOW);
  }
  btn.onSinglePress(onBtnSingle);
  btn.onLongPress(onBtnLong);

#ifndef USE_EMBEDDED_SEQ
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(10);

  Serial.println("Initializing SD...");
  Serial.flush();

  setLED(0, 80, 60, 0); setLED(1, 80, 60, 0);  // yellow: SD initializing

  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("SD init failed!");
    Serial.flush();
    haltWithBlink(255, 0, 0);  // red: SD init failed
  }

  Serial.println("SD init OK");
  Serial.flush();

  LEDSeqState* states[] = { &s0, &s1 };
  for (int i = 0; i < 2; i++) {
    FsFile f = sd.open(states[i]->filename, O_RDONLY);
    if (!f) {
      Serial.print(states[i]->filename); Serial.println(" missing!");
      haltWithBlink(255, 0, 255);  // magenta: file missing
    }
    f.close();
  }
#else
  Serial.println("Embedded sequences in flash");
#endif

  for (int i = 0; i < 2; i++) {
    setLED(0, 0, 255, 0); setLED(1, 0, 255, 0); delay(200);
    setLED(0, 0,   0, 0); setLED(1, 0,   0, 0); delay(200);
  }

  Serial.println("Ready. Press button to start, or type HELP for serial commands.");
}

// ---------------------- Loop -----------------------------
void loop() {
  btn.update();

  if (!g_canStart) { g_buttonEdge = false; return; }

  unsigned long now = millis();
  bool run0 = stepSeq(s0, now);
  bool run1 = stepSeq(s1, now);
  g_buttonEdge = false;

  // Prefetch next command into RAM during the slack of the current one.
  // Zero-wait at transition time; SD read hidden inside cur's duration.
  tryPrefetchNext(s0);
  tryPrefetchNext(s1);

  if (!run0 && !run1) {
    g_canStart = false;
    setLED(0, 0, 0, 0);
    setLED(1, 0, 0, 0);
    Serial.println("Sequence done");
  }
}
