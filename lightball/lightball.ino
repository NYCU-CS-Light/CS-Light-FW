#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "types.h"

#ifndef M_PI
#define M_PI 3.14159265f
#endif

// ---------------------- User Config ----------------------
const int BTN_PIN   = A4;
const int SD_CS_PIN = 8;

// ---------------------- LED Pins -------------------------
const LEDPins LED_PINS[2] = { {9, 6, 11}, {13, 5, 10} };

void setLED(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(LED_PINS[id].r, r);
  analogWrite(LED_PINS[id].g, g);
  analogWrite(LED_PINS[id].b, b);
}

void haltWithBlink(uint8_t r, uint8_t g, uint8_t b) {
  while (1) {
    setLED(0, r, g, b); setLED(1, r, g, b); delay(200);
    setLED(0, 0, 0, 0); setLED(1, 0, 0, 0); delay(200);
  }
}

// ---------------------- Runtime State --------------------
LEDSeqState s0(0, "led0.txt");
LEDSeqState s1(1, "led1.txt");

// Shared button edge flag — set once per loop(), consumed by CMD_WAIT
static bool g_buttonEdge = false;

// ---------------------- SD Helpers -----------------------
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

bool advanceCmd(LEDSeqState &st, unsigned long now) {
  char buf[64];
  while (st.file.available()) {
    int len = st.file.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[len] = '\0';
    LEDCommand next;
    if (parseCommandLine(buf, next)) {
      st.cmd        = next;
      st.cmdStartMs = now;
      logCmd(st.ledId, next);
      return true;
    }
  }
  st.file.close();
  st.active = false;
  Serial.print("[LED"); Serial.print(st.ledId); Serial.println("] END");
  return false;
}

// ---------------------- Sequence Runtime -----------------
void startSeq(LEDSeqState &st) {
  if (st.file) st.file.close();
  st.file = SD.open(st.filename, FILE_READ);
  if (!st.file) {
    Serial.print("Failed to open "); Serial.println(st.filename);
    st.active = false;
    return;
  }
  st.loopsLeft = -1;
  st.active    = advanceCmd(st, millis());
}

bool stepSeq(LEDSeqState &st) {
  if (!st.active) return false;

  uint8_t       ledId   = st.ledId;
  unsigned long now     = millis();
  const LEDCommand &cmd = st.cmd;
  unsigned long elapsed = now - st.cmdStartMs;

  switch (cmd.type) {

    case CMD_COLOR: {
      setLED(ledId, cmd.r, cmd.g, cmd.b);
      if (cmd.durationMs == 0 || elapsed >= cmd.durationMs)
        advanceCmd(st, now);
    } break;

    case CMD_BLINK: {
      if (elapsed >= cmd.durationMs) { advanceCmd(st, now); break; }
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
        advanceCmd(st, now);
        break;
      }
      float a = (elapsed >= cmd.durationMs) ? 1.0f : (float)elapsed / (float)cmd.durationMs;
      setLED(ledId,
        (uint8_t)((1.0f - a) * cmd.r  + a * cmd.r2),
        (uint8_t)((1.0f - a) * cmd.g  + a * cmd.g2),
        (uint8_t)((1.0f - a) * cmd.b  + a * cmd.b2));
      if (elapsed >= cmd.durationMs) advanceCmd(st, now);
    } break;

    case CMD_BREATHE: {
      if (elapsed >= cmd.durationMs) { advanceCmd(st, now); break; }
      if (cmd.onMs == 0) { setLED(ledId, cmd.r, cmd.g, cmd.b); break; }
      float t = (float)(elapsed % cmd.onMs) / (float)cmd.onMs;
      float a = (1.0f - cosf(2.0f * (float)M_PI * t)) * 0.5f;
      setLED(ledId,
        (uint8_t)(a * cmd.r),
        (uint8_t)(a * cmd.g),
        (uint8_t)(a * cmd.b));
    } break;

    case CMD_PINGPONG: {
      if (elapsed >= cmd.durationMs) { advanceCmd(st, now); break; }
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
        Serial.print("[LED"); Serial.print(ledId); Serial.println("] WAIT -> btn");
        advanceCmd(st, now);
      }
    } break;

    case CMD_LOOP: {
      if (cmd.durationMs == 0) {
        // infinite loop
        Serial.print("[LED"); Serial.print(ledId); Serial.println("] LOOP rewind");
        st.file.seek(0);
        advanceCmd(st, now);
      } else {
        if (st.loopsLeft < 0) st.loopsLeft = (int16_t)(cmd.durationMs - 1);
        if (st.loopsLeft > 0) {
          st.loopsLeft--;
          Serial.print("[LED"); Serial.print(ledId);
          Serial.print("] LOOP rewind left="); Serial.println(st.loopsLeft);
          st.file.seek(0);
          advanceCmd(st, now);
        } else {
          Serial.print("[LED"); Serial.print(ledId); Serial.println("] LOOP done");
          st.loopsLeft = -1;
          st.file.close();
          st.active = false;
        }
      }
    } break;

    default:
      advanceCmd(st, now);
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

void cmdList() {
  File root = SD.open("/");
  while (true) {
    File e = root.openNextFile();
    if (!e) break;
    Serial.print("  "); Serial.print(e.name());
    Serial.print("  "); Serial.print(e.size()); Serial.println(" B");
    e.close();
  }
  root.close();
}

void cmdCat(const char* fn) {
  File f = SD.open(fn, FILE_READ);
  if (!f) { Serial.print("ERR: not found: "); Serial.println(fn); return; }
  while (f.available()) Serial.write(f.read());
  f.close();
  Serial.println();
  Serial.println("---END---");
}

void cmdWrite(const char* fn) {
  if (SD.exists(fn)) SD.remove(fn);
  File f = SD.open(fn, FILE_WRITE);
  if (!f) { Serial.print("ERR: cannot open: "); Serial.println(fn); return; }
  Serial.print("READY "); Serial.println(fn);
  Serial.println("(one line per send, OR use ';' as line separator; end with 'END')");
  uint16_t lines = 0;
  while (true) {
    if (readSerialLine()) {
      if (strcmp(g_serialBuf, "END") == 0) break;
      f.println(g_serialBuf);
      lines++;
    }
  }
  f.close();
  Serial.print("DONE "); Serial.print(lines); Serial.println(" lines");
}

void cmdDel(const char* fn) {
  if (SD.remove(fn)) { Serial.print("Deleted "); Serial.println(fn); }
  else               { Serial.print("ERR: cannot delete: "); Serial.println(fn); }
}

void cmdHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  LIST            list files"));
  Serial.println(F("  CAT <file>      print file"));
  Serial.println(F("  WRITE <file>    overwrite file; send lines then 'END'"));
  Serial.println(F("  DEL <file>      delete file"));
  Serial.println(F("  HELP            this list"));
  Serial.println(F("  (button press)  start sequence"));
}

void handleSerialCommand() {
  if (!readSerialLine()) return;
  if      (strncmp(g_serialBuf, "WRITE ", 6) == 0) cmdWrite(g_serialBuf + 6);
  else if (strncmp(g_serialBuf, "CAT ",   4) == 0) cmdCat  (g_serialBuf + 4);
  else if (strncmp(g_serialBuf, "DEL ",   4) == 0) cmdDel  (g_serialBuf + 4);
  else if (strcmp (g_serialBuf, "LIST")      == 0) cmdList();
  else if (strcmp (g_serialBuf, "HELP")      == 0) cmdHelp();
  else if (g_serialBuf[0] != '\0') {
    Serial.print("Unknown: '"); Serial.print(g_serialBuf); Serial.println("' (try HELP)");
  }
}

// ---------------------- Button ---------------------------
bool buttonPressedEdge() {
  static int last = HIGH;
  static unsigned long lastChange = 0;
  static int stableLast = HIGH;

  int cur = digitalRead(BTN_PIN);
  unsigned long now = millis();

  if (cur != last) { lastChange = now; last = cur; }

  if ((now - lastChange) > 30 && cur != stableLast) {
    stableLast = cur;
    if (stableLast == LOW) return true;
  }
  return false;
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
  pinMode(BTN_PIN, INPUT_PULLUP);

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(10);

  Serial.println("Initializing SD...");
  Serial.flush();

  setLED(0, 80, 60, 0); setLED(1, 80, 60, 0);  // yellow: SD initializing

  if (!SD.begin(SPI_HALF_SPEED, SD_CS_PIN)) {
    Serial.println("SD init failed!");
    Serial.flush();
    haltWithBlink(255, 0, 0);  // red: SD init failed
  }

  Serial.println("SD init OK");
  Serial.flush();

  LEDSeqState* states[] = { &s0, &s1 };
  for (int i = 0; i < 2; i++) {
    File f = SD.open(states[i]->filename, FILE_READ);
    if (!f) {
      Serial.print(states[i]->filename); Serial.println(" missing!");
      haltWithBlink(255, 0, 255);  // magenta: file missing
    }
    f.close();
  }

  for (int i = 0; i < 2; i++) {
    setLED(0, 0, 255, 0); setLED(1, 0, 255, 0); delay(200);
    setLED(0, 0,   0, 0); setLED(1, 0,   0, 0); delay(200);
  }

  Serial.println("Ready. Press button to start, or type HELP for serial commands.");
}

// ---------------------- Loop -----------------------------
void loop() {
  static bool canStart = false;
  bool edge = buttonPressedEdge();

  if (!canStart) {
    handleSerialCommand();
    if (edge) {
      canStart = true;
      startSeq(s0);
      startSeq(s1);
      Serial.println("Sequence start");
    }
    return;
  }

  g_buttonEdge = edge;
  bool run0 = stepSeq(s0);
  bool run1 = stepSeq(s1);
  g_buttonEdge = false;

  if (!run0 && !run1) {
    canStart = false;
    setLED(0, 0, 0, 0);
    setLED(1, 0, 0, 0);
    Serial.println("Sequence done");
  }
}
