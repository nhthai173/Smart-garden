//
// Created by Thái Nguyễn on 18/7/24.
//

#include "GenericOutputBase.h"

stdGenericOutput::GenericOutputBase::GenericOutputBase(uint8_t pin, bool activeState) {
    _pin = pin;
    _activeState = activeState;
    _state = false;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !_activeState);
}

#ifdef USE_PCF8574
stdGenericOutput::GenericOutputBase::GenericOutputBase(PCF8574& pcf8574, uint8_t pin, bool activeState) {
    _pin = pin;
    _activeState = activeState;
    _state = false;
    _pcf8574 = &pcf8574;
    pcf8574.pinMode(_pin, OUTPUT);
    pcf8574.digitalWrite(_pin, !_activeState);
}
#endif

stdGenericOutput::GenericOutputBase::~GenericOutputBase() {
    _onPowerChanged = nullptr;
    _onPowerOff = nullptr;
    _onPowerOn = nullptr;

#if defined(USE_FIREBASE_RTDB)
    _databaseConfig = nullptr;
    _fbCallback = nullptr;
#endif

}

void stdGenericOutput::GenericOutputBase::_write() {
#if defined(USE_PCF8574)
    if (_pcf8574 != nullptr) {
        _pcf8574->digitalWrite(_pin, _state ? _activeState : !_activeState);
    } else {
        digitalWrite(_pin, _state ? _activeState : !_activeState);
    }
#else
    digitalWrite(_pin, _state ? _activeState : !_activeState);
#endif
}

void stdGenericOutput::GenericOutputBase::on() {
    if (_state) return;
    _state = true;
    _write();
    if (_onPowerOn != nullptr) {
        _onPowerOn();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
#if defined(USE_FIREBASE_RTDB)
    if (_rtdbPath.length() > 0) {
        _setRTDBState();
    }
#endif
}

void stdGenericOutput::GenericOutputBase::off() {
    if (!_state) return;
    _state = false;
    _write();
    if (_onPowerOff != nullptr) {
        _onPowerOff();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
#if defined(USE_FIREBASE_RTDB)
    if (_rtdbPath.length() > 0) {
        _setRTDBState();
    }
#endif
}

void stdGenericOutput::GenericOutputBase::toggle() {
    if (_state) {
        off();
    } else {
        on();
    }
}

void stdGenericOutput::GenericOutputBase::setState(bool state) {
    if (state) {
        on();
    } else {
        off();
    }
}

void stdGenericOutput::GenericOutputBase::setState(const String& state) {
    if (state.startsWith("ON")) {
        on();
    } else {
        off();
    }
}

void stdGenericOutput::GenericOutputBase::setActiveState(bool activeState) {
    _activeState = activeState;
}

void stdGenericOutput::GenericOutputBase::setStartUpState(startup_state_t startUpState) {
    _startUpState = startUpState;
}

bool stdGenericOutput::GenericOutputBase::getActiveState() const {
    return _activeState;
}

bool stdGenericOutput::GenericOutputBase::getState() const {
    return _state;
}

String stdGenericOutput::GenericOutputBase::getStateString() const {
    return _state ? "ON" : "OFF";
}

stdGenericOutput::startup_state_t stdGenericOutput::GenericOutputBase::getStartUpState() const {
    return _startUpState;
}