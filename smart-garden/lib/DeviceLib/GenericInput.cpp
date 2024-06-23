//
// Created by Thái Nguyễn on 23/6/24.
//

#include "GenericInput.h"


GenericInput::GenericInput(uint8_t pin, uint8_t mode, bool activeState, bool useInterrupt) {
    pinMode(pin, mode);
    _pin = pin;
    _activeState = activeState;
    _lastState = digitalRead(_pin);
    _useInterrupt = useInterrupt;
    if (useInterrupt) {
        _attch();
    }
}

void GenericInput::_IRQHandler(void *arg) {
    GenericInput *instance = (GenericInput *) arg;
    if (instance->_lastState == instance->_activeState && digitalRead(instance->_pin) != instance->_activeState) {
        if (instance->_onInactiveCB != nullptr) {
            instance->_onInactiveCB();
        }
    } else if (instance->_lastState != instance->_activeState &&
               digitalRead(instance->_pin) == instance->_activeState) {
        if (instance->_onActiveCB != nullptr) {
            instance->_onActiveCB();
        }
    }
    if (instance->_onChangeCB != nullptr) {
        instance->_onChangeCB();
    }
}