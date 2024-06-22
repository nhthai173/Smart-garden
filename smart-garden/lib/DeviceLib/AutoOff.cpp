#include "AutoOff.h"

void AutoOff::on()
{
    if (_state) return;
    _previousMillis = millis();
    _state = true;
    digitalWrite(_pin, _activeState);
    if (_onPowerOn != nullptr)
    {
        _onPowerOn();
    }
}

void AutoOff::off()
{
    if (!_state) return;
    _state = false;
    _previousMillis = MAXUL;
    digitalWrite(_pin, !_activeState);
    if (_onPowerOff != nullptr)
    {
        _onPowerOff();
    }
}

void AutoOff::setDuration(unsigned long duration)
{
    _duration = duration;
}

unsigned long AutoOff::getDuration()
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