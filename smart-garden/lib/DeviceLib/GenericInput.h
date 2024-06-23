//
// Created by Thái Nguyễn on 23/6/24.
//

#ifndef GENERICINPUT_H
#define GENERICINPUT_H

#include <Arduino.h>

class GenericInput {

public:
    GenericInput() {};

    /**
     * @brief Construct a new GenericInput object
     * @param pin pin number
     * @param mode pin mode. Default is INPUT
     * @param activeState active state. Default is LOW
     * @param useInterrupt use interrupt or not. Default is true
     */
    GenericInput(uint8_t pin, uint8_t mode = INPUT, bool activeState = LOW, bool useInterrupt = true);

    /**
     * @brief The interrupt handler. Do not call or override this function
     * @param arg the instance of the object
     */
    static void _IRQHandler(void *arg);

    /**
     * @brief Use interrupt or not
     * @param useInterrupt
     */
    void useInterrupt(bool useInterrupt) {
        _useInterrupt = useInterrupt;
        if (useInterrupt) {
            _attch();
        } else {
            _detach();
        }
    }

    /**
     * @brief Set the pin mode
     * @param mode
     */
    void setMode(uint8_t mode) {
        _detach();
        pinMode(_pin, mode);
        if (_useInterrupt) {
            _attch();
        }
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

protected:
    uint8_t _pin;
    bool _lastState;
    bool _activeState;
    bool _useInterrupt = false;
    String _activeStateStr = "ACTIVE";
    String _inactiveStateStr = "NONE";
    std::function<void()> _onChangeCB = nullptr;
    std::function<void()> _onActiveCB = nullptr;
    std::function<void()> _onInactiveCB = nullptr;

    void _attch() {
        attachInterruptArg(digitalPinToInterrupt(_pin), _IRQHandler, (void *) this, CHANGE);
    }
    void _detach() {
        detachInterrupt(digitalPinToInterrupt(_pin));
    }
};


#endif //GENERICINPUT_H
