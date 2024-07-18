#include "GenericOutput.h"

namespace stdGenericOutput {
    void GenericOutput::on()
    {
        if (_pOnDelay > 0 && _pState != ON)
        {
            _previousMillis = millis();
            _pState = WAIT_FOR_ON;
            return;
        }
        _pState = ON;
        _previousMillis = millis();
        if (_state) return;
        GenericOutputBase::on();
    }

    void GenericOutput::on(uint32_t duration) {
        _onceTimeDuration = duration;
        on();
    }

    void GenericOutput::onPercentage(uint8_t percentage) {
        if (percentage < 1 || percentage > 100) return;
        on(_duration * percentage / 100);
    }

    void GenericOutput::off()
    {
        _pState = OFF;
        if (!_state) return;
        GenericOutputBase::off();
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

    void GenericOutput::onAutoOff(std::function<void()> onAutoOff) {
        _onAutoOff = std::move(onAutoOff);
    }

    void GenericOutput::loop()
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
}