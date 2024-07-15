//
// Created by Thái Nguyễn on 23/6/24.
//

#ifndef GENERICINPUT_H
#define GENERICINPUT_H

#include <Arduino.h>
#include <Ticker.h>

#if __has_include_next(<PCF8574.h>)

#include <PCF8574.h>

#ifndef USE_PCF8574
#define USE_PCF8574
#endif // USE_PCF8574
#endif // __has_include_next(<PCF8574.h>)

class GenericInput {

public:
    GenericInput() = default;

    /**
     * @brief Construct a new GenericInput object
     * @param pin pin number
     * @param mode pin mode. Default is INPUT
     * @param activeState active state. Default is LOW
     */
    explicit GenericInput(uint8_t pin, uint8_t mode = INPUT, bool activeState = LOW, uint32_t debounceTime = 50, bool useInterrupt = true);

#if defined(USE_PCF8574)

    /**
     * @brief Construct a new GenericInput object
     * @param pcf8574 PCF8574 object
     * @param pin pin number
     * @param mode pin mode. Default is INPUT
     * @param activeState active state. Default is LOW
     */
    explicit GenericInput(PCF8574 &pcf8574, uint8_t pin, uint8_t mode = INPUT, bool activeState = LOW, uint32_t debounceTime = 50);

#endif

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
    void setMode(uint8_t mode) {
        _setMode(mode);
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
    bool getState() {
        return _read() == _activeState;
    }

    String getStateString() {
        return getState() ? _activeStateStr : _inactiveStateStr;
    }

    /**
     * @brief Set the label for active state
     * @param activeStateStr
     */
    void setActiveStateString(const String &activeStateStr) {
        _activeStateStr = activeStateStr;
    }

    /**
     * @brief Set the label for inactive state
     * @param inactiveStateStr
     */
    void setInactiveStateString(const String &inactiveStateStr) {
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

    /**
     * @brief attach interrupt
     * @param mode
     * @return
     */
    bool attachInterrupt(uint8_t mode) {
        if (digitalPinToInterrupt(_pin) < 0)
            return false;
        detachInterrupt();
        ::attachInterruptArg(_pin, _interruptHandler, this, mode);
        return true;
    }

    /**
     * @brief detach interrupt
     */
    void detachInterrupt() {
        ::detachInterrupt(digitalPinToInterrupt(_pin));
    }

protected:
    Ticker _ticker;
    uint8_t _pin;
    bool _lastState;
    bool _activeState;
    uint32_t _debounceTime;
    String _activeStateStr = "ACTIVE";
    String _inactiveStateStr = "NONE";
    std::function<void()> _onChangeCB = nullptr;
    std::function<void()> _onActiveCB = nullptr;
    std::function<void()> _onInactiveCB = nullptr;

#if defined(USE_PCF8574)
    PCF8574 *_pcf8574 = nullptr;

    /**
     * @brief pinMode wrapper
     */
    void _setMode(uint8_t mode) {
        if (_pcf8574 != nullptr) {
            _pcf8574->pinMode(_pin, mode);
        } else {
            pinMode(_pin, mode);
        }
    }

    /**
     * @brief digitalRead wrapper
     */
    uint8_t _read() {
        if (_pcf8574 != nullptr) {
            return _pcf8574->digitalRead(_pin);
        } else {
            return digitalRead(_pin);
        }
    }

#else

    /**
     * @brief pinMode wrapper
     */
    void _setMode(uint8_t mode) {
        pinMode(_pin, mode);
    }

    /**
     * @brief digitalRead wrapper
     */
    uint8_t _read() {
        return digitalRead(_pin);
    }

#endif

    /**
     * @brief Interrupt handler
     * @param arg GenericInput object
     */
    IRAM_ATTR void static _interruptHandler(void *arg) {
        auto *self = (GenericInput *) arg;
        self->_ticker.detach();
        if (self->_debounceTime > 0) {
            self->_ticker.once_ms(self->_debounceTime, _debounceHandler, self);
        } else {
            _debounceHandler(self);
        }
    }

    /**
     * @brief Handler after debounce time
     * @param pInput GenericInput object
     */
    void static _debounceHandler(GenericInput *pInput) {
        bool currentState = pInput->_read();
        if (currentState != pInput->_lastState) {
            pInput->_lastState = currentState;
            if (currentState == pInput->_activeState) {
                if (pInput->_onActiveCB != nullptr) {
                    pInput->_onActiveCB();
                }
            } else {
                if (pInput->_onInactiveCB != nullptr) {
                    pInput->_onInactiveCB();
                }
            }
            if (pInput->_onChangeCB != nullptr) {
                pInput->_onChangeCB();
            }
        }
    }

}; // class GenericInput


#endif //GENERICINPUT_H
