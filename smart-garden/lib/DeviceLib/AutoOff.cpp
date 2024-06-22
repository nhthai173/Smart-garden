#include "AutoOff.h"

void AutoOff::on()
{
    _state = true;
    digitalWrite(_pin, _activeState);
    _previousMillis = millis();
    if (_onPowerOn != nullptr)
    {
        _onPowerOn();
    }
}

void AutoOff::off()
{
    _state = false;
    digitalWrite(_pin, !_activeState);
    if (_onPowerOff != nullptr)
    {
        _onPowerOff();
    }
}

void AutoOff::setDuration(uint32_t duration)
{
    _duration = duration;
}

uint32_t AutoOff::getDuration()
{
    return _duration;
}

void AutoOff::loop()
{
    if (_state)
    {
        if (millis() - _previousMillis >= _duration)
        {
            off();
        }
    }
}

void AutoOff::onPowerOn(std::function<void()> onPowerOn)
{
    _onPowerOn = onPowerOn;
}

void AutoOff::onPowerOff(std::function<void()> onPowerOff)
{
    _onPowerOff = onPowerOff;
}