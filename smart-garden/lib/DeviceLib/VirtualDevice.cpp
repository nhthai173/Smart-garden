//
// Created by Thái Nguyễn on 22/6/24.
//

#include "VirtualDevice.h"

void VirtualDevice::on() {
    if (_onFunction != nullptr) {
        _onFunction();
    }
    _state = true;
}

void VirtualDevice::off() {
    if (_offFunction != nullptr) {
        _offFunction();
    }
    _state = false;
}