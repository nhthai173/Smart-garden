//
// Created by Thái Nguyễn on 15/7/24.
//

#include "VoltageReader.h"

VoltageReader::VoltageReader(uint8_t pin, float r1, float r2, float changeThreshold, float minVolt, float maxVolt) {
    pinMode(pin, INPUT);
    this->pin = pin;
    this->R1 = r1;
    this->R2 = r2;
    this->changeThreshold = changeThreshold;
    this->minVolt = minVolt;
    this->maxVolt = maxVolt;
    this->lastVolt = 0;
}

void VoltageReader::setSafeThreshold(float min, float max) {
    this->minVolt = min;
    this->maxVolt = max;
}

float VoltageReader::get() {
    return lastVolt;
}

float VoltageReader::_read() {
    float v = analogRead(pin) / 4095.0 * 3.3;
    if (R1 > 0 && R2 > 0) {
        return v * (R1 + R2) / R2;
    }
    return v;
}

void VoltageReader::loop() {
    float v = _read();
    if (abs(v - lastVolt) > changeThreshold) {
        lastVolt = v;
        if (minVolt > 0 && v < minVolt && _onLow != nullptr) {
            _onLow();
        } else if (maxVolt > 0 && v > maxVolt && _onHigh != nullptr) {
            _onHigh();
        } else if (_onChanged != nullptr) {
            _onChanged();
        }
    }
}