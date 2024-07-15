//
// Created by Thái Nguyễn on 23/6/24.
//

#include "GenericInput.h"


GenericInput::GenericInput(uint8_t pin, uint8_t mode, bool activeState, uint32_t debounceTime, bool useInterrupt) {
    pinMode(pin, mode);
    _pin = pin;
    _activeState = activeState;
    _lastState = digitalRead(_pin);
    _debounceTime = debounceTime;
    if (useInterrupt) {
        attachInterrupt(CHANGE);
    }
}


#if defined(USE_PCF8574)
GenericInput::GenericInput(PCF8574 &pcf8574, uint8_t pin, uint8_t mode, bool activeState, uint32_t debounceTime) {
    _pcf8574 = &pcf8574;
    _pin = pin;
    _activeState = activeState;
    _lastState = _pcf8574->digitalRead(_pin);
    _debounceTime = debounceTime;
}
#endif