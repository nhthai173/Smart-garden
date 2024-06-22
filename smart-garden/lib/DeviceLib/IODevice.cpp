#include "IODevice.h"

IODevice::IODevice(uint8_t pin, bool activeState) {
    _pin = pin;
    _activeState = activeState;
    _state = false;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !_activeState);
}

void IODevice::on() {
    digitalWrite(_pin, _activeState);
    _state = true;
}

void IODevice::off() {
    digitalWrite(_pin, !_activeState);
    _state = false;
}

void IODevice::toggle() {
    if (_state) {
        off();
    } else {
        on();
    }
}

void IODevice::setState(String state) {
    if (state.startsWith("ON")) {
        on();
    } else {
        off();
    }
}

void IODevice::setActiveState(bool activeState) {
    _activeState = activeState;
}

bool IODevice::getActiveState() {
    return _activeState;
}

bool IODevice::getState() {
    return _state;
}

String IODevice::getStateString() {
    return _state ? "ON" : "OFF";
}