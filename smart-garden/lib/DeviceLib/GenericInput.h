//
// Created by Thái Nguyễn on 23/6/24.
//

#ifndef GENERICINPUT_H
#define GENERICINPUT_H

#include <Arduino.h>
#include <vector>

struct GI_hold_state_cb_t {
    bool state;
    uint32_t time;
    std::function<void()> callback;
    bool executed;
};

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
     * @brief Destroy the GenericInput object
     */
    ~GenericInput();

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

    /**
     * @brief Set the callback function when hold state
     * @param state state to hold
     * @param time time to hold in milliseconds
     * @param callback callback function
     */
    void onHoldState(bool state, uint32_t time, std::function<void()> callback) {
        _allHoldCBExecuted = false;

        // Callback list is empty
        if (_holdStateCBs.empty()) {
            _holdStateCBs.push_back({state, time, std::move(callback), false});
            return;
        }

        // Existing callback for this state and time -> update callback
        for (auto &cb : _holdStateCBs) {
            if (cb.state == state && cb.time == time) {
                cb.callback = std::move(callback);
                return;
            }
        }

        // New callback
        _holdStateCBs.push_back({state, time, std::move(callback), false});
    }

    bool deleteHoldState(bool state, uint32_t time) {
        if (_holdStateCBs.empty())
            return false;
        for (auto it = _holdStateCBs.begin(); it != _holdStateCBs.end(); ++it) {
            if (it->state == state && it->time == time) {
                _holdStateCBs.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Loop function to be called in the main loop
     */
    void loop();

protected:
    uint8_t _pin = 0;
    bool _lastState = false;
    bool _activeState = false;
    bool _lastReadState = false; // for debounce
    uint32_t _debounceTime = 50;
    unsigned long _lastDebounceTime = 0;
    String _activeStateStr = "ACTIVE";
    String _inactiveStateStr = "NONE";
    std::function<void()> _onChangeCB = nullptr;
    std::function<void()> _onActiveCB = nullptr;
    std::function<void()> _onInactiveCB = nullptr;

    uint32_t _lastActiveTime = 0;
    uint32_t _lastInactiveTime = 0;
    bool _allHoldCBExecuted = false;
    std::vector<GI_hold_state_cb_t> _holdStateCBs;
};


#endif //GENERICINPUT_H
