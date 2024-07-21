#ifndef GENERIC_OUTPUT_H
#define GENERIC_OUTPUT_H

#include "GenericOutputBase.h"
#include <vector>

namespace stdGenericOutput {

    typedef enum {
        OFF = 0x00,
        ON = 0x01,
        WAIT_FOR_ON = 0x02,
    } state_t;

    class GenericOutput;
}

class stdGenericOutput::GenericOutput: public stdGenericOutput::GenericOutputBase
{
public:
    GenericOutput() = default;


    /**
     * @brief Construct a new Auto Off object
     *
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     * @param duration duration to turn off after power is on in milliseconds
     */
    explicit GenericOutput(uint8_t pin, bool activeState = LOW, uint32_t duration = 0) : GenericOutputBase(pin, activeState) {
        _autoOffEnabled = false;
        _duration = duration;
        if (duration > 0)
            _autoOffEnabled = true;
    }

#if defined(USE_PCF8574)

    /**
     * @brief Construct a new Auto Off object
     *
     * @param pcf8574 PCF8574 object
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     * @param duration duration to turn off after power is on in milliseconds
     */
    explicit GenericOutput(PCF8574& pcf8574, uint8_t pin, bool activeState = LOW, uint32_t duration = 0) : GenericOutputBase(pcf8574, pin, activeState) {
        _autoOffEnabled = false;
        _duration = duration;
        if (duration > 0)
            _autoOffEnabled = true;
    }

#endif

    ~GenericOutput() {
        _onAutoOff = nullptr;
    }

    /**
     * @brief set power on
     * @param force true to force power on
     */
    void on(bool force) override;

    /**
     * @brief set power on
     */
    void on() override {
        on(false);
    }

    /**
     * @brief set power on for a duration. Only works once, the next time on with default duration
     * @param duration milliseconds
     * @param force true to force power on
     */
    void onOnce(uint32_t duration, bool force = false);

    /**
     * @brief set power on with percentage timing (0-100%) based on the duration
     * @param percentage 1-100
     * @param force true to force power on
     */
    void onPercentage(uint8_t percentage, bool force = false);

    /**
     * @brief set power off immediately
     * @param force true to force power off
     */
    void off(bool force) override;

    /**
     * @brief set power off
     */
    void off() override {
        off(false);
    }

    /**
     * @brief set a delay before power on. Set to 0 to disable
     *
     * @param delay milliseconds
     */
    void setPowerOnDelay(uint32_t delay);

    /**
     * @brief enable or disable auto off
     * @param autoOffEnabled true to enable, false to disable
     */
    void setAutoOff(bool autoOffEnabled);

    /**
     * @brief enable or disable auto off and set the duration
     *
     * @param autoOffEnabled
     * @param duration
     */
    void setAutoOff(bool autoOffEnabled, uint32_t duration);

    /**
     * @brief Set the duration to turn off after the power is on
     *
     * @param duration
     */
    void setDuration(uint32_t duration);

    /**
     * @brief Get the Duration object
     *
     * @return uint32_t
     */
    uint32_t getDuration() const;

    /**
     * @brief Get the Power On Delay object
     *
     * @return uint32_t
     */
    uint32_t getPowerOnDelay() const;

    /**
     * @brief Get the Remaining Time in milliseconds
     * @return
     */
    uint32_t getRemainingTime() const {
        if (_state) {
            uint32_t t = millis() - _previousMillis;
            if (t < _duration)
                return _duration - t;
            return 0;
        }
        return 0;
    }

    String getRemainingTimeString() const {
        uint32_t remaining = getRemainingTime();
        if (remaining == 0)
            return "00:00";
        uint32_t minutes = remaining / 60000;
        uint32_t seconds = (remaining % 60000) / 1000;
        return String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);
    }

    /**
     * @brief Set the callback function to be called when power is turned off automatically
     *
     * @param onAutoOff callback function
     */
    void onAutoOff(std::function<void()> onAutoOff);

    /**
     * @brief function to be called in loop
     *
     */
    void loop();

protected:
    bool _autoOffEnabled = false;
    state_t _pState = stdGenericOutput::OFF;
    uint32_t _duration = 0;
    uint32_t _onceTimeDuration = 0;
    uint32_t _pOnDelay = 0;
    uint32_t _previousMillis = 0;
    std::function<void()> _onAutoOff = nullptr;
};


using stdGenericOutput::GenericOutput;



#endif // GENERIC_OUTPUT_H