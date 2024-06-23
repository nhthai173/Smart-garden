#include "IODevice.h"

IODevice::IODevice(uint8_t pin, bool activeState) {
    _pin = pin;
    _activeState = activeState;
    _state = false;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !_activeState);
}

void IODevice::on() {
    if (_state) return;
    _state = true;
    digitalWrite(_pin, _activeState);
    if (_onPowerOn != nullptr) {
        _onPowerOn();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}

void IODevice::off() {
    if (!_state) return;
    _state = false;
    digitalWrite(_pin, !_activeState);
    if (_onPowerOff != nullptr) {
        _onPowerOff();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}

void IODevice::toggle() {
    if (_state) {
        off();
    } else {
        on();
    }
}

void IODevice::setState(const String& state) {
    if (state.startsWith("ON")) {
        on();
    } else {
        off();
    }
}

void IODevice::setActiveState(bool activeState) {
    _activeState = activeState;
}

bool IODevice::getActiveState() const {
    return _activeState;
}

bool IODevice::getState() const {
    return _state;
}

String IODevice::getStateString() const {
    return _state ? "ON" : "OFF";
}