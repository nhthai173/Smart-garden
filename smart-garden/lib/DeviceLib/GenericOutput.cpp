#include "GenericOutput.h"

void GenericOutput::on()
{
    if (_state) return;
    if (_pOnDelay > 0 && _pState != stdGenericOutput::ON)
    {
        _pState = stdGenericOutput::WAIT_FOR_ON;
        _ticker.detach();
        _ticker.once_ms(_pOnDelay, _onTick, this);
        return;
    }
    _pState = stdGenericOutput::ON;
    GenericOutputBase::on();
    if (_autoOffEnabled && _duration > 0)
    {
        _ticker.detach();
        _ticker.once_ms(_duration, _onTick, this);
    }
}

void GenericOutput::on(uint32_t duration) {
    _pState = stdGenericOutput::ON;
    GenericOutputBase::on();
    if (_autoOffEnabled)
    {
        _ticker.detach();
        _ticker.once_ms(duration, _onTick, this);
    }
}

void GenericOutput::onPercentage(uint8_t percentage) {
    if (percentage < 1 || percentage > 100) return;
    on(_duration * percentage / 100);
}

void GenericOutput::off()
{
    if (!_state) return;
    _pState = stdGenericOutput::OFF;
    GenericOutputBase::off();
    _ticker.detach();
}

void GenericOutput::setPowerOnDelay(uint32_t delay)
{
    _pOnDelay = delay;
}

void GenericOutput::setAutoOff(bool autoOffEnabled) {
    _autoOffEnabled = autoOffEnabled;
}

void GenericOutput::setAutoOff(bool autoOffEnabled, uint32_t duration) {
    _autoOffEnabled = autoOffEnabled;
    _duration = duration;
}

void GenericOutput::setDuration(uint32_t duration)
{
    _duration = duration;
    if (duration > 0)
        _autoOffEnabled = true;
    else
        _autoOffEnabled = false;
}

uint32_t GenericOutput::getDuration() const
{
    return _duration;
}

uint32_t GenericOutput::getPowerOnDelay() const {
    return _pOnDelay;
}