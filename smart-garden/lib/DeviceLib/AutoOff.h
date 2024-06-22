#ifndef AUTO_OFF_H
#define AUTO_OFF_H

#define MAXUL 4294967295

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
    AutoOff(uint8_t pin, unsigned long duration, bool activeState = LOW) : IODevice(pin, activeState) {
        _duration = duration;
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
    void setDuration(unsigned long duration);
    
    /**
     * @brief Get the Duration object
     * 
     * @return unsigned long
     */
    unsigned long getDuration();
    
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
    unsigned long _duration;
    unsigned long _previousMillis = MAXUL;
    std::function<void()> _onPowerOn = nullptr;
    std::function<void()> _onPowerOff = nullptr;
};

#endif // AUTO_OFF_H