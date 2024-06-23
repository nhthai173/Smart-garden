//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualDevice.h"

void VirtualDevice::on() {
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

void VirtualDevice::off() {
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