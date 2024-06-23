//
// Created by Thái Nguyễn on 23/6/24.
//

#ifndef GENERICINPUT_H
#define GENERICINPUT_H

#include <Arduino.h>

class GenericInput {

public:
    GenericInput() = default;

    /**
     * @brief Construct a new GenericInput object
     * @param pin pin number
     * @param mode pin mode. Default is INPUT
     * @param activeState active state. Default is LOW
     */
    explicit GenericInput(uint8_t pin, uint8_t mode = INPUT, bool activeState = LOW, uint32_t debounceTime = 50);

    /**
     * @brief Set the pin number
     * @param pin
     */
    void setPin(uint8_t pin) {
        _pin = pin;
    }

    /**
     * @brief Get the pin number
     * @return uint8_t
     */
    uint8_t getPin() const {
        return _pin;
    }

    /**
     * @brief Set the pin mode
     * @param mode
     */
    void setMode(uint8_t mode) const {
        pinMode(_pin, mode);
    }

    /**
     * @brief Set the debounce time
     * @param debounceTime
     */
    void setDebounceTime(uint32_t debounceTime) {
        _debounceTime = debounceTime;
    }

    /**
     * @brief Get the debounce time
     * @return uint32_t
     */
    uint32_t getDebounceTime() const {
        return _debounceTime;
    }

    /**
     * @brief Set the active state
     * @param activeState
     */
    void setActiveState(bool activeState) {
        _activeState = activeState;
    }

    /**
     * @brief Get the active state
     * @return true HIGH
     * @return false LOW
     */
    bool getActiveState() const {
        return _activeState;
    }

    /**
     * @brief Get the current state of the device
     * @return true when active
     * @return false when inactive
     */
    bool getState() const {
        return digitalRead(_pin) == _activeState;
    }

    String getStateString() const {
        return digitalRead(_pin) == _activeState ? _activeStateStr : _inactiveStateStr;
    }

    /**
     * @brief Set the label for active state
     * @param activeStateStr
     */
    void setActiveStateString(const String& activeStateStr) {
        _activeStateStr = activeStateStr;
    }

    /**
     * @brief Set the label for inactive state
     * @param inactiveStateStr
     */
    void setInactiveStateString(const String& inactiveStateStr) {
        _inactiveStateStr = inactiveStateStr;
    }

    /**
     * @brief Set the callback function when state changed
     * @param cb
     */
    void onChange(std::function<void()> cb) {
        _onChangeCB = std::move(cb);
    }

    /**
     * @brief Set the callback function when active
     * @param cb
     */
    void onActive(std::function<void()> cb) {
        _onActiveCB = std::move(cb);
    }

    /**
     * @brief Set the callback function when inactive
     * @param cb
     */
    void onInactive(std::function<void()> cb) {
        _onInactiveCB = std::move(cb);
    }

    void loop();

protected:
    uint8_t _pin;
    bool _lastState;
    bool _activeState;
    bool _lastReadState; // for debounce
    uint32_t _debounceTime;
    unsigned long _lastDebounceTime = 0;
    String _activeStateStr = "ACTIVE";
    String _inactiveStateStr = "NONE";
    std::function<void()> _onChangeCB = nullptr;
    std::function<void()> _onActiveCB = nullptr;
    std::function<void()> _onInactiveCB = nullptr;
};


#endif //GENERICINPUT_H
