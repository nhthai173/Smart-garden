#ifndef GENERIC_OUTPUT_H
#define GENERIC_OUTPUT_H

#include <Arduino.h>

class GenericOutput {

public:

    GenericOutput() = default;

    /**
     * @brief Construct a new GenericOutput object
     * 
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit GenericOutput(uint8_t pin, bool activeState = LOW);

    /**
     * @brief Set powe to ON
     * 
     */
    virtual void on();

    /**
     * @brief Set power to OFF
     * 
     */
    virtual void off();

    /**
     * @brief Toggle power
     * 
     */
    void toggle();

    /**
     * @brief Set the power state
     * @param state true to turn on, false to turn off
     */
    void setState(bool state);

    /**
     * @brief Set the power state from string "ON" or "OFF"
     * 
     * @param state 
     */
    void setState(const String& state);

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
    bool getActiveState() const;

    /**
     * @brief Get current state of the device
     * 
     * @return true when ON
     * @return false when OFF
     */
    bool getState() const;

    /**
     * @brief Get current state of the device as string "ON" or "OFF"
     *
     * @return String
     
     */
    String getStateString() const;

    /**
     * @brief Set callback function to be called when power is on
     *
     * @param onPowerOn
     */
    void onPowerOn(std::function<void()> onPowerOn) {
        _onPowerOn = std::move(onPowerOn);
    }

    /**
     * @brief Set callback function to be called when power is off
     *
     * @param onPowerOff
     */
    void onPowerOff(std::function<void()> onPowerOff) {
        _onPowerOff = std::move(onPowerOff);
    }

    /**
     * @brief Set callback function to be called when power is changed
     *
     * @param onPowerChanged
     */
    void onPowerChanged(std::function<void()> onPowerChanged) {
        _onPowerChanged = std::move(onPowerChanged);
    }

protected:
    uint8_t _pin;
    bool _activeState;
    bool _state;
    std::function<void()> _onPowerOn = nullptr;
    std::function<void()> _onPowerOff = nullptr;
    std::function<void()> _onPowerChanged = nullptr;

};

#endif // GENERIC_OUTPUT_H