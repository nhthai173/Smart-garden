//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualOutput.h"

void VirtualOutput::on(bool force) {
    _onceTimeDuration = 0;
    if (_pOnDelay > 0 && _pState != stdGenericOutput::ON) {
        _previousMillis = millis();
        _pState = stdGenericOutput::WAIT_FOR_ON;
        return;
    }
    _pState = stdGenericOutput::ON;
    _previousMillis = millis();

    if (!force && _state) return;
    _state = true;
    if (_onFunction != nullptr) {
        Serial.printf("> P[%d] -> Von\n", _pin);
        _onFunction();
//        start_callback(_onFunction, String("von_" + String(_pin)).c_str());
    }
    // Callbacks
    if (_onPowerOn != nullptr) {
        Serial.printf("> P[%d] -> Pon\n", _pin);
        _onPowerOn();
//        start_callback(_onPowerOn, String("pon_" + String(_pin)).c_str());
    }
    if (_onPowerChanged != nullptr) {
        Serial.printf("> P[%d] -> Pchange\n", _pin);
        _onPowerChanged();
//        start_callback(_onPowerChanged, String("pchange_" + String(_pin)).c_str());
    }
    if (_startUpState != stdGenericOutput::START_UP_NONE) {
        setLastState();
    }
#ifdef USE_FIREBASE_RTDB
    if (_rtdbPath.length()) {
        _setRTDBState();
    }
#endif
}

void VirtualOutput::off(bool force) {
    _onceTimeDuration = 0;
    _pState = stdGenericOutput::OFF;
    if (!force && !_state) return;
    _state = false;
    if (_offFunction != nullptr) {
        Serial.printf("> P[%d] -> Voff\n", _pin);
        _offFunction();
//        start_callback(_offFunction, String("voff_" + String(_pin)).c_str());
    }
    // Callbacks
    if (_onPowerOff != nullptr) {
        Serial.printf("> P[%d] -> Poff\n", _pin);
        _onPowerOff();
//        start_callback(_onPowerOff, String("poff_" + String(_pin)).c_str());
    }
    if (_onPowerChanged != nullptr) {
        Serial.printf("> P[%d] -> Pchange\n", _pin);
        _onPowerChanged();
//        start_callback(_onPowerChanged, String("pchange_" + String(_pin)).c_str());
    }
    if (_startUpState != stdGenericOutput::START_UP_NONE) {
        setLastState();
    }
#ifdef USE_FIREBASE_RTDB
    if (_rtdbPath.length()) {
        _setRTDBState();
    }
#endif
}