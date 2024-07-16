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

void GenericInput::loop() {
    bool currentState = digitalRead(_pin);
    if (currentState != _lastReadState) {
        _lastDebounceTime = millis();
    }
    if (millis() - _lastDebounceTime >= _debounceTime) {
        if (currentState == _lastState) return;
        _lastState = currentState;
        _holdStateCBs[!currentState].lastTime = 0;
        _holdStateCBs[currentState].lastTime = millis();
        for(auto &cb : _holdStateCBs[currentState].callbacks) {
            cb.executed = false;
        }

        if (currentState == _activeState) {
            if (_onActiveCB != nullptr) {
                _onActiveCB();
            }
        } else {
            if (_onInactiveCB != nullptr) {
                _onInactiveCB();
            }
        }
        if (_onChangeCB != nullptr) {
            _onChangeCB();
        }
    }
    _lastReadState = currentState;

    if (_holdStateCBs[currentState].lastTime > 0) {
        for (auto &cb : _holdStateCBs[currentState].callbacks) {
            if (millis() - cb.time >= _holdStateCBs[currentState].lastTime) {
                if (!cb.executed) {
                    cb.callback();
                    cb.executed = true;
                }
            }
        }
    }
}