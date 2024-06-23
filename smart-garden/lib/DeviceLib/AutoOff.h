#ifndef AUTO_OFF_H
#define AUTO_OFF_H

#include <Arduino.h>
#include "GenericOutput.h"

class AutoOff : public GenericOutput
{
public:

    AutoOff() = default;

    /**
     * @brief Construct a new Auto Off object
     * 
     * @param pin pin number
     * @param duration duration to turn off after power is on in milliseconds
     * @param activeState LOW or HIGH. Default is LOW
     */
    AutoOff(uint8_t pin, unsigned long duration, bool activeState = LOW, bool autoOffEnabled = true) : GenericOutput(pin, activeState) {
        _duration = duration;
        _autoOffEnabled = autoOffEnabled;
    }
    
    /**
     * @brief set power on
     * 
     */
    void on() override;

    /**
     * @brief set power off immediately
     * 
     */
    void off() override;

    /**
     * @brief enable or disable auto off
     * @param autoOffEnabled true to enable, false to disable
     */
    void setAutoOff(bool autoOffEnabled) {
        _autoOffEnabled = autoOffEnabled;
    }

    /**
     * @brief enable or disable auto off and set the duration
     *
     * @param autoOffEnabled
     * @param duration
     */
    void setAutoOff(bool autoOffEnabled, unsigned long duration) {
        _autoOffEnabled = autoOffEnabled;
        _duration = duration;
    }

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
    unsigned long _duration;
    unsigned long _previousMillis = 0;
    std::function<void()> _onAutoOff = nullptr;
};

#endif // AUTO_OFF_H