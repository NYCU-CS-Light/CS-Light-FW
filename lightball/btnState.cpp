#include "btnState.h"

btnState::btnState(int pin)
  : _pin(pin),
    _rawLast(HIGH),
    _stable(HIGH),
    _lastChangeMs(0),
    _state(S_IDLE),
    _pressStartMs(0),
    _releaseMs(0),
    _longFired(false),
    _onSingle(nullptr),
    _onDouble(nullptr),
    _onLong(nullptr)
{
  pinMode(_pin, INPUT_PULLUP);
  _rawLast = digitalRead(_pin);
  _stable  = _rawLast;
}

void btnState::onSinglePress(Callback cb) { _onSingle = cb; }
void btnState::onDoublePress(Callback cb) { _onDouble = cb; }
void btnState::onLongPress(Callback cb)   { _onLong   = cb; }

bool btnState::_debounce() {
  int raw = digitalRead(_pin);
  unsigned long now = millis();

  if (raw != _rawLast) {
    _lastChangeMs = now;
    _rawLast = raw;
  }

  if ((now - _lastChangeMs) > DEBOUNCE_MS && raw != _stable) {
    _stable = raw;
    return true;
  }
  return false;
}

void btnState::update() {
  unsigned long now = millis();
  bool edge = _debounce();

  switch (_state) {

    case S_IDLE:
      if (edge && _stable == LOW) {
        _pressStartMs = now;
        _longFired    = false;
        _state        = S_PRESSED;
      }
      break;

    case S_PRESSED:
      if (!_longFired && (now - _pressStartMs) >= LONG_PRESS_MS) {
        _longFired = true;
        if (_onLong) _onLong();
      }
      if (edge && _stable == HIGH) {
        if (_longFired) {
          _state = S_IDLE;
        } else {
          _releaseMs = now;
          _state     = S_WAIT_DOUBLE;
        }
      }
      break;

    case S_WAIT_DOUBLE:
      if (edge && _stable == LOW) {
        _pressStartMs = now;
        _state        = S_DOUBLE_PRESSED;
      } else if ((now - _releaseMs) >= DOUBLE_WINDOW_MS) {
        if (_onSingle) _onSingle();
        _state = S_IDLE;
      }
      break;

    case S_DOUBLE_PRESSED:
      if (edge && _stable == HIGH) {
        if (_onDouble) _onDouble();
        _state = S_IDLE;
      }
      break;
  }
}
