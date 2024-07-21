//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualOutput.h"

void VirtualOutput::on(bool force) {
    if (_pOnDelay > 0 && _pState != stdGenericOutput::ON)
    {
        _previousMillis = millis();
        _pState = stdGenericOutput::WAIT_FOR_ON;
        return;
    }
    _pState = stdGenericOutput::ON;

    if (!force && _state) return;
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
#ifdef USE_FIREBASE_RTDB
    _setRTDBState();
#endif
}

void VirtualOutput::off(bool force) {
    _pState = stdGenericOutput::OFF;
    if (!force && !_state) return;
    _state = false;
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
#ifdef USE_FIREBASE_RTDB
    _setRTDBState();
#endif
}