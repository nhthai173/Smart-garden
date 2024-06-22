#ifndef IO_DEVICE_H
#define IO_DEVICE_H

#include <Arduino.h>

class IODevice {

public:

    IODevice() { };

    /**
     * @brief Construct a new IODevice object
     * 
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    IODevice(uint8_t pin, bool activeState = 0);

    /**
     * @brief Set powe to ON
     * 
     */
    void on();

    /**
     * @brief Set power to OFF
     * 
     */
    void off();

    /**
     * @brief Toggle power
     * 
     */
    void toggle();

    /**
     * @brief Set the power state from string "ON" or "OFF"
     * 
     * @param state 
     */
    void setState(String state);

    /**
     * @brief Set the Active State object
     * 
     * @param activeState 
     */
    void setActiveState(bool activeState);

    /**
     * @brief Get the Active State object
     * 
     * @return true 
     * @return false 
     */
    bool getActiveState();

    /**
     * @brief Get current state of the device
     * 
     * @return true when ON
     * @return false when OFF
     */
    bool getState();

    /**
     * @brief Get current state of the device as string "ON" or "OFF"
     *
     * @return String
     
     */
    String getStateString();

protected:
    uint8_t _pin;
    bool _activeState;
    bool _state;

};

#endif // IO_DEVICE_H