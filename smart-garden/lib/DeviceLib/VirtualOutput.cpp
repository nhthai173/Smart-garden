//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualOutput.h"

void VirtualOutput::on() {
    if (_state) return;
    if (_pOnDelay > 0 && _pState != stdGenericOutput::ON)
    {
        _pState = stdGenericOutput::WAIT_FOR_ON;
        _ticker.detach();
        _ticker.once_ms(_pOnDelay, _onTick, this);
        return;
    }
    _pState = stdGenericOutput::ON;
    _state = true;
    if (_autoOffEnabled && _duration > 0) {
        _ticker.detach();
        _ticker.once_ms(_duration, _onTick, this);
    }
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
    _pState = stdGenericOutput::OFF;
    _ticker.detach();
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