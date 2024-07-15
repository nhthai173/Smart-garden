#ifndef AUTO_OFF_H
#define AUTO_OFF_H

#include <Arduino.h>
#include "GenericOutput.h"

class AutoOff : public GenericOutput
{
public:

    enum State {
        OFF = 0x00,
        ON = 0x01,
        WAIT_FOR_ON = 0x02,
    };

    AutoOff() = default;

    /**
     * @brief Construct a new Auto Off object
     * 
     * @param pin pin number
     * @param duration duration to turn off after power is on in milliseconds
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit AutoOff(uint8_t pin, bool activeState = LOW, unsigned long duration = 0) : GenericOutput(pin, activeState) {
        _autoOffEnabled = false;
        _duration = duration;
        if (duration > 0)
            _autoOffEnabled = true;
    }
    
    /**
     * @brief set power on
     * 
     */
    void on() override;

    /**
     * @brief set power on for a duration. Only works once
     *
     * @param duration
     */
    void on(unsigned long duration);

    /**
     * @brief set power on with percentage timing (0-100%) based on the duration
     * @param percentage 1-100
     */
    void onPercentage(uint8_t percentage);

    /**
     * @brief set power off immediately
     * 
     */
    void off() override;

    /**
     * @brief set a delay before power on. Set to 0 to disable
     *
     * @param delay milliseconds
     */
    void setPowerOnDelay(unsigned long delay);

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
    void setAutoOff(bool autoOffEnabled, unsigned long duration);

    /**
     * @brief Set the duration to turn off after the power is on
     * 
     * @param duration 
     */
    void setDuration(unsigned long duration);
    
    /**
     * @brief Get the Duration object
     * 
     * @return unsigned long
     */
    unsigned long getDuration() const;

    /**
     * @brief Set the callback function to be called when power is turned off automatically
     *
     * @param onAutoOff callback function
     */
    void onAutoOff(std::function<void()> onAutoOff) {
        _onAutoOff = std::move(onAutoOff);
    }
    
    /**
     * @brief function to be called in loop
     * 
     */
    void loop();

protected:
    bool _autoOffEnabled = false;
    uint8_t _pState = OFF;
    unsigned long _duration = 0;
    unsigned long _onceTimeDuration = 0;
    unsigned long _pOnDelay = 0;
    unsigned long _previousMillis = 0;
    std::function<void()> _onAutoOff = nullptr;
};

#endif // AUTO_OFF_H