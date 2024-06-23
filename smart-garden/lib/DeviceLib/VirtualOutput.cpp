//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualOutput.h"

void VirtualOutput::on() {
    _state = true;
    if (_onFunction != nullptr) {
        _onFunction();
    }
    if (_onPowerOn != nullptr) {
        _onPowerOn();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}

void VirtualOutput::off() {
    _state = false;
    if (_offFunction != nullptr) {
        _offFunction();
    }
    if (_onPowerOff != nullptr) {
        _onPowerOff();
    }
    if (_onPowerChanged != nullptr) {
        _onPowerChanged();
    }
}