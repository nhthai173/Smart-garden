//
// Created by Thái Nguyễn on 15/7/24.
//

#ifndef SMART_GARDEN_VOLTAGEREADER_H
#define SMART_GARDEN_VOLTAGEREADER_H

#include <Arduino.h>

class VoltageReader {
public:
    uint8_t pin = 0;
    float R1 = 0;
    float R2 = 0;
    float changeThreshold = 0.1;
    float minVolt = -1;
    float maxVolt = -1;
    float lastVolt = 0;

    VoltageReader(uint8_t pin, float r1 = 0, float r2 = 0, float changeThreshold = 0.1, float minVolt = -1, float maxVolt = -1);
    void setSafeThreshold(float min, float max);
    float get();
    void loop();

    void onChanged(std::function<void()> callback) {
        _onChanged = callback;
    }

    void onUnsafe(std::function<void()> callback) {
        _onUnsafe = callback;
    }

    void onLow(std::function<void()> callback) {
        _onLow = callback;
    }

    void onHigh(std::function<void()> callback) {
        _onHigh = callback;
    }
private:
    std::function<void()> _onChanged;
    std::function<void()> _onUnsafe;
    std::function<void()> _onLow;
    std::function<void()> _onHigh;
    float _read();
};


#endif //SMART_GARDEN_VOLTAGEREADER_H
