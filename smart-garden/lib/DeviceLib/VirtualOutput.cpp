//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualOutput.h"

void VirtualOutput::on() {
    if (_state) return;
    if (_pOnDelay > 0 && _pState != ON)
    {
        _previousMillis = millis();
        _pState = WAIT_FOR_ON;
        return;
    }
    _pState = ON;
    _state = true;
    _previousMillis = millis();
    if (_onFunction != nullptr) {
        _onFunction();
    }
    // Callbacks
    if (_onPowerOn != nullptr) {
        _onPowerOn();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}

void VirtualOutput::off() {
    if (!_state) return;
    _state = false;
    _pState = OFF;
    if (_offFunction != nullptr) {
        _offFunction();
    }
    // Callbacks
    if (_onPowerOff != nullptr) {
        _onPowerOff();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}