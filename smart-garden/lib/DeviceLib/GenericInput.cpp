//
// Created by Thái Nguyễn on 23/6/24.
//

#include "GenericInput.h"


GenericInput::GenericInput(uint8_t pin, uint8_t mode, bool activeState, uint32_t debounceTime) {
    pinMode(pin, mode);
    _pin = pin;
    _activeState = activeState;
    _lastState = digitalRead(pin);
    _lastReadState = _lastState;
    _debounceTime = debounceTime;
    _lastDebounceTime = millis();
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
        GI_DEBUG_PRINT("[%d] has been changed to: %d (%s)\n", _pin, _lastState, isActive ? "ACTIVE" : "INACTIVE");
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
    }
    _lastReadState = currentState;

    if (!_allHoldCBExecuted) {
        uint32_t lastChangeTime = isActive ? _lastActiveTime : _lastInactiveTime;
        GI_DEBUG_PRINT("Checking hold state callbacks\n");
        uint8_t pendingCnt = 0;
        for (auto &cb: _holdStateCBs) {
            if (cb.state == isActive) {
                if (cb.executed) continue;
                pendingCnt++;
                if (millis() - lastChangeTime >= cb.time) {
                    GI_DEBUG_PRINT("> Executing hold state callback: %dms\n", cb.time);
                    if (cb.callback)
                        cb.callback();
                    cb.executed = true;
                }
            }
        }
        if (!pendingCnt) {
            _allHoldCBExecuted = true;
        }

#ifdef USE_FIREBASE_RTDB
        if (millis() - lastChangeTime >= _reportStateDelay) {
            _setRTDBState();
        } else {
            _allHoldCBExecuted = false;
        }
#endif

        GI_DEBUG_PRINT("> Pending hold state callbacks: %d\n", pendingCnt);
    } // !_allHoldCBExecuted
} // loop