#include "AutoOff.h"

void AutoOff::on()
{
    if (_state) return;
    _previousMillis = millis();
    GenericOutput::on();
}

void AutoOff::off()
{
    if (!_state) return;
    GenericOutput::off();
}

void AutoOff::setDuration(unsigned long duration)
{
    _duration = duration;
}

unsigned long AutoOff::getDuration() const
{
    return _duration;
}

void AutoOff::loop()
{
    if (_autoOffEnabled && _state)
    {
        if (millis() >= _previousMillis + _duration)
        {
            off();
            if (_onAutoOff != nullptr)
            {
                _onAutoOff();
            }
        }
    }
}