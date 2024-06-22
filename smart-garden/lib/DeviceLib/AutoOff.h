#ifndef AUTO_OFF_H
#define AUTO_OFF_H

#include <Arduino.h>
#include "IODevice.h"

class AutoOff : public IODevice
{
public:

    /**
     * @brief Construct a new Auto Off object
     * 
     * @param pin pin number
     * @param duration duration to turn off after power is on in milliseconds
     * @param activeState LOW or HIGH. Default is LOW
     */
    AutoOff(uint8_t pin, uint32_t duration, bool activeState = 0) : IODevice(pin, activeState) {
        _duration = duration;
        _previousMillis = 0;
    }
    
    /**
     * @brief set power on
     * 
     */
    void on();

    /**
     * @brief set power off immediately
     * 
     */
    void off();

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
    uint32_t getDuration();
    
    /**
     * @brief function to be called in loop
     * 
     */
    void loop();

    /**
     * @brief Set callback function to be called when power is on
     * 
     * @param onPowerOn 
     */
    void onPowerOn(std::function<void()> onPowerOn);

    /**
     * @brief Set callback function to be called when power is off
     * 
     * @param onPowerOff 
     */
    void onPowerOff(std::function<void()> _onPowerOff);
protected:
    uint32_t _duration;
    uint32_t _previousMillis;
    std::function<void()> _onPowerOn = nullptr;
    std::function<void()> _onPowerOff = nullptr;
};

#endif // AUTO_OFF_H