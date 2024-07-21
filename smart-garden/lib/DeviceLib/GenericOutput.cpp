#include "GenericOutput.h"

namespace stdGenericOutput {
    void GenericOutput::on(bool force) {
        _onceTimeDuration = 0;
        if (_pOnDelay > 0 && _pState != ON) {
            _previousMillis = millis();
            _pState = WAIT_FOR_ON;
            return;
        }
        _pState = ON;
        _previousMillis = millis();
        GenericOutputBase::on(force);
    }

    void GenericOutput::onOnce(uint32_t duration, bool force) {
        on(force);
        _onceTimeDuration = duration;
    }

    void GenericOutput::onPercentage(uint8_t percentage, bool force) {
        if (percentage < 1 || percentage > 100) return;
        onOnce(_duration * percentage / 100, force);
    }

    void GenericOutput::off(bool force) {
        _onceTimeDuration = 0;
        _pState = OFF;
        GenericOutputBase::off(force);
    }

    void GenericOutput::setPowerOnDelay(uint32_t delay) {
        _pOnDelay = delay;
    }

    void GenericOutput::setAutoOff(bool autoOffEnabled) {
        _autoOffEnabled = autoOffEnabled;
    }

    void GenericOutput::setAutoOff(bool autoOffEnabled, uint32_t duration) {
        _autoOffEnabled = autoOffEnabled;
        _duration = duration;
    }

    void GenericOutput::setDuration(uint32_t duration) {
        _duration = duration;
        if (duration > 0)
            _autoOffEnabled = true;
        else
            _autoOffEnabled = false;
    }

    uint32_t GenericOutput::getDuration() const {
        return _duration;
    }

    uint32_t GenericOutput::getPowerOnDelay() const {
        return _pOnDelay;
    }

    void GenericOutput::onAutoOff(std::function<void()> onAutoOff) {
        _onAutoOff = std::move(onAutoOff);
    }

    void GenericOutput::loop() {
        if (_onceTimeDuration > 0 && _state) {
            if (millis() >= _previousMillis + _onceTimeDuration) {
                _onceTimeDuration = 0;
                off(false);
                if (_onAutoOff != nullptr) {
                    _onAutoOff();
                }
            }
        } else if (_autoOffEnabled && _state) {
            if (millis() >= _previousMillis + _duration) {
                off(false);
                if (_onAutoOff != nullptr) {
                    _onAutoOff();
                }
            }
        }

        if (_pState == WAIT_FOR_ON && millis() >= _previousMillis + _pOnDelay) {
            _pState = ON;
            on(false);
        }
    }
}