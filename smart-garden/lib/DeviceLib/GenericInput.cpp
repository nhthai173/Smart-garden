//
// Created by Thái Nguyễn on 23/6/24.
//

#include "GenericInput.h"


GenericInput::GenericInput(uint8_t pin, uint8_t mode, bool activeState, uint32_t debounceTime) {
    pinMode(pin, mode);
    _pin = pin;
    _activeState = activeState;
    _lastState = digitalRead(_pin);
    _lastReadState = _lastState;
    _debounceTime = debounceTime;
}

GenericInput::~GenericInput() {
    _onChangeCB = nullptr;
    _onActiveCB = nullptr;
    _onInactiveCB = nullptr;
}

void GenericInput::loop() {
    bool currentState = digitalRead(_pin);
    bool isActive = currentState == _activeState;

    if (currentState != _lastReadState) {
        _lastDebounceTime = millis();
    }
    if (millis() - _lastDebounceTime >= _debounceTime && currentState != _lastState) {
        _lastState = currentState;
        _allHoldCBExecuted = false;
        if (isActive) {
            _lastActiveTime = millis();
            if (_onActiveCB != nullptr) {
                _onActiveCB();
            }
        } else {
            _lastInactiveTime = millis();
            if (_onInactiveCB != nullptr) {
                _onInactiveCB();
            }
        }
        for (auto &cb: _holdStateCBs) {
            if (cb.state == isActive) {
                cb.executed = false;
            }
        }
        if (_onChangeCB != nullptr) {
            _onChangeCB();
        }
        Serial.printf("[%d] Change state: %d\n", this->_pin, isActive);
    }
    _lastReadState = currentState;

    if (!_allHoldCBExecuted) {
        uint8_t pendingCnt = 0;
        for (auto &cb: _holdStateCBs) {
            if (cb.state == isActive) {
                if (cb.executed) continue;
                pendingCnt++;
                uint32_t time = isActive ? _lastActiveTime : _lastInactiveTime;
                if (millis() - time >= cb.time) {
                    if (cb.callback)
                        cb.callback();
                    cb.executed = true;
                }
            }
        }
        if (!pendingCnt) {
            _allHoldCBExecuted = true;
        }
    }
}