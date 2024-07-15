#include "AutoOff.h"

void AutoOff::on()
{
    if (_state) return;
    if (_pOnDelay > 0 && _pState != ON)
    {
        _previousMillis = millis();
        _pState = WAIT_FOR_ON;
        return;
    }
    _pState = ON;
    _previousMillis = millis();
    GenericOutput::on();
}

void AutoOff::on(unsigned long duration) {
    _onceTimeDuration = duration;
    on();
}

void AutoOff::onPercentage(uint8_t percentage) {
    if (percentage < 1 || percentage > 100) return;
    on(_duration * percentage / 100);
}

void AutoOff::off()
{
    if (!_state) return;
    _pState = OFF;
    GenericOutput::off();
}

void AutoOff::setPowerOnDelay(unsigned long delay)
{
    _pOnDelay = delay;
}

void AutoOff::setAutoOff(bool autoOffEnabled) {
    _autoOffEnabled = autoOffEnabled;
}

void AutoOff::setAutoOff(bool autoOffEnabled, unsigned long duration) {
    _autoOffEnabled = autoOffEnabled;
    _duration = duration;
}

void AutoOff::setDuration(unsigned long duration)
{
    _duration = duration;
    if (duration > 0)
        _autoOffEnabled = true;
    else
        _autoOffEnabled = false;
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
    else if (_onceTimeDuration > 0 && _state) {
        if (millis() >= _previousMillis + _onceTimeDuration) {
            _onceTimeDuration = 0;
            off();
            if (_onAutoOff != nullptr)
            {
                _onAutoOff();
            }
        }
    }
    if (_pState == WAIT_FOR_ON && millis() >= _previousMillis + _pOnDelay)
    {
        _pState = ON;
        on();
    }
}