//
// Created by Thái Nguyễn on 23/6/24.
//

#ifndef GENERICINPUT_H
#define GENERICINPUT_H

#include <Arduino.h>
#include <vector>

#define DEBUG_GENERIC_INPUT

#ifdef DEBUG_GENERIC_INPUT
#define GI_DEBUG_PRINT(...) Serial.printf(__VA_ARGS__)
#else
#define GI_DEBUG_PRINT(...)
#endif // DEBUG_GENERIC_INPUT





#if __has_include(<FirebaseClient.h>)

#include <FirebaseClient.h>
#include "FirebaseRTDBIntegrate.h"

#ifndef USE_FIREBASE_RTDB
#define USE_FIREBASE_RTDB
#endif // USE_FIREBASE_RTDB

#endif // __has_include(<FirebaseClient.h>)




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
        bool currentState = _lastState == _activeState;

        // Callback list is empty
        if (_holdStateCBs.empty()) {
            // executed = true if current state is the same as the last state to prevent callback executed immediately
            _holdStateCBs.push_back({state, time, std::move(callback), state == currentState});
            return;
        }

        // Existing callback for this state and time -> update callback
        for (auto &cb : _holdStateCBs) {
            if (cb.state == state && cb.time == time) {
                cb.callback = std::move(callback);
                cb.executed = state == currentState;
                return;
            }
        }

        // New callback
        _holdStateCBs.push_back({state, time, std::move(callback), state == currentState});
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
    


    // Firebase RTDB
#ifdef USE_FIREBASE_RTDB

    /**
     * @brief Attach Firebase RTDB to the device
     * @param database_config
     * @param path the sub path for the device
     * @param fbCallback The callback function to be called when the state is updated to the database
     *
     * Call syncState(bool) to set the state received from database on first connect.
     * If startupState is START_UP_LAST_STATE, the last state from database is high priority than FS last state. If startupState is START_UP_NONE, device will set to database the last state from FS.
     */
    void
    attachDatabase(fbrtdb_object *database_config, const String &path, AsyncResultCallback fbCallback = nullptr) {
        _databaseConfig = database_config;
        _rtdbPath = path;
        _generateTaskId();
        _fbCallback = fbCallback;
    }

    /**
     * @brief Get the sub path for the device
     * 
     * @return String 
     */
    String getDatabasePath() const {
        return _rtdbPath;
    }

    /**
     * @brief Get the full path for the device
     * 
     * @param path 
     */
    String getDatabaseFullPath() const {
        return _databaseConfig->prefixPath + _rtdbPath;
    }

    /**
     * @brief Get the delay time to report state to the database when state is changed
     * Default is 200ms
     */
    uint32_t getDatabaseReportStateDelay() const {
        return _reportStateDelay;
    }

    void setDatabasePath(const String &path) {
        _rtdbPath = path;
        _generateTaskId();
    }

    void setDatabaseCallback(AsyncResultCallback *fbCallback) {
        _fbCallback = reinterpret_cast<AsyncResultCallback>(fbCallback);
    }

    /**
     * @brief Set the Database Report State Delay object
     * 
     * @param delay ms
     */
    void setDatabaseReportStateDelay(uint32_t delay) {
        _reportStateDelay = delay;
    }

#endif // USE_FIREBASE_RTDB



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


#ifdef USE_FIREBASE_RTDB
    fbrtdb_object *_databaseConfig = nullptr;
    String _rtdbPath;
    String _rtdbTaskId;
    uint32_t _reportStateDelay = 200;
    AsyncResultCallback _fbCallback = nullptr;

    void _generateTaskId() {
        _rtdbTaskId = _rtdbPath;
        _rtdbTaskId.replace("/", "");
        _rtdbTaskId.replace(".", "_");
        _rtdbTaskId.replace(" ", "_");
    }

    void _setRTDBState() {
        if (_databaseConfig != nullptr && _rtdbPath.length() > 0) {
            _databaseConfig->rtdb->set(
                    *_databaseConfig->client,
                    _databaseConfig->prefixPath + _rtdbPath,
                    digitalRead(_pin) == _activeState,
                    _fbCallback,
                    _rtdbTaskId);
        }
    }

#endif // USE_FIREBASE_RTDB



}; // class GenericInput


#endif //GENERICINPUT_H
