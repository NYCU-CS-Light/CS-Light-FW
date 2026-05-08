#pragma once
#ifndef H_BTNSTATE
#define H_BTNSTATE

#include <Arduino.h>

class btnState {
public:
  typedef void (*Callback)();

  explicit btnState(int pin);

  void onSinglePress(Callback cb);
  void onDoublePress(Callback cb);
  void onLongPress(Callback cb);

  void update();

private:
  int           _pin;

  int           _rawLast;
  int           _stable;
  unsigned long _lastChangeMs;

  enum State : uint8_t { S_IDLE, S_PRESSED, S_WAIT_DOUBLE, S_DOUBLE_PRESSED };
  State         _state;
  unsigned long _pressStartMs;
  unsigned long _releaseMs;
  bool          _longFired;

  Callback _onSingle;
  Callback _onDouble;
  Callback _onLong;

  static const unsigned long DEBOUNCE_MS      = 30UL;
  static const unsigned long LONG_PRESS_MS    = 3000UL;
  static const unsigned long DOUBLE_WINDOW_MS = 300UL;

  bool _debounce();
};

#endif
