#include "GenericOutput.h"

GenericOutput::GenericOutput(uint8_t pin, bool activeState) {
    _pin = pin;
    _activeState = activeState;
    _state = false;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !_activeState);
}

void GenericOutput::on() {
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

void GenericOutput::off() {
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

void GenericOutput::toggle() {
    if (_state) {
        off();
    } else {
        on();
    }
}

void GenericOutput::setState(bool state) {
    if (state) {
        on();
    } else {
        off();
    }
}

void GenericOutput::setState(const String& state) {
    if (state.startsWith("ON")) {
        on();
    } else {
        off();
    }
}

void GenericOutput::setActiveState(bool activeState) {
    _activeState = activeState;
}

bool GenericOutput::getActiveState() const {
    return _activeState;
}

bool GenericOutput::getState() const {
    return _state;
}

String GenericOutput::getStateString() const {
    return _state ? "ON" : "OFF";
}