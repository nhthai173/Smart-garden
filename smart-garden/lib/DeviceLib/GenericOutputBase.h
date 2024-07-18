#ifndef SMART_GARDEN_GENERICOUTPUTBASE_H
#define SMART_GARDEN_GENERICOUTPUTBASE_H

#include <Arduino.h>

#if __has_include(<PCF8574.h>)
#include <PCF8574.h>
#ifndef USE_PCF8574
#define USE_PCF8574
#endif // USE_PCF8574
#endif // __has_include(<PCF8574.h>)

#if __has_include(<FirebaseClient.h>)

#include <FirebaseClient.h>
#include "FirebaseRTDBIntegrate.h"

#ifndef USE_FIREBASE_RTDB
#define USE_FIREBASE_RTDB
#endif // USE_FIREBASE_RTDB

#endif // __has_include(<FirebaseClient.h>)

namespace stdGenericOutput {

    typedef enum {
        START_UP_OFF = 0x00,
        START_UP_ON = 0x01,
        START_UP_LAST_STATE = 0x02,
    } startup_state_t;

    class GenericOutputBase;

} // stdGenericOutput



class stdGenericOutput::GenericOutputBase {

public:

    GenericOutputBase() = default;

    /**
     * @brief Construct a new GenericOutputBase object
     *
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit GenericOutputBase(uint8_t pin, bool activeState = LOW);

#if defined(USE_PCF8574)
    /**
     * @brief Construct a new GenericOutputBase object
     *
     * @param pcf8574 PCF8574 object
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit GenericOutputBase(PCF8574& pcf8574, uint8_t pin, bool activeState = LOW);
#endif

    /**
     * @brief Destroy the GenericOutputBase object
     */
    ~GenericOutputBase();

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
    void setState(const String &state);

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
     * @brief Set the start up state
     * @param startUpState
     */
    void setStartUpState(startup_state_t startUpState);

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
     * @brief Set the callback function to be called when power is turned off automatically
     * @return
     */
    startup_state_t getStartUpState() const;

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

#if defined(USE_FIREBASE_RTDB)

    // @TODO implement start up state
    void
    attachDatabase(fbrtdb_object *database_config, const String &path, startup_state_t startupState = START_UP_OFF,
                   AsyncResultCallback fbCallback = nullptr) {
        _databaseConfig = database_config;
        _rtdbPath = path;
        _generateTaskId();
        _startUpState = startupState;
        _fbCallback = fbCallback;
    }

    String getDatabasePath() const {
        return _rtdbPath;
    }

    String getDatabaseFullPath() const {
        return _databaseConfig->prefixPath + _rtdbPath;
    }

    void setDatabasePath(const String &path) {
        _rtdbPath = path;
        _generateTaskId();
    }

    void setDatabaseCallback(AsyncResultCallback *fbCallback) {
        _fbCallback = reinterpret_cast<AsyncResultCallback>(fbCallback);
    }

#endif // USE_FIREBASE_RTDB

protected:
    uint8_t _pin;
    bool _activeState;
    bool _state;
    startup_state_t _startUpState = START_UP_OFF;
    std::function<void()> _onPowerOn = nullptr;
    std::function<void()> _onPowerOff = nullptr;
    std::function<void()> _onPowerChanged = nullptr;

#if defined(USE_PCF8574)
    PCF8574* _pcf8574 = nullptr;
#endif

#if defined(USE_FIREBASE_RTDB)
    fbrtdb_object *_databaseConfig = nullptr;
    String _rtdbPath;
    String _rtdbTaskId;
    AsyncResultCallback _fbCallback = nullptr;

    void _generateTaskId() {
        _rtdbTaskId = _rtdbPath;
        _rtdbTaskId.replace("/", "");
        _rtdbTaskId.replace(".", "_");
        _rtdbTaskId.replace(" ", "_");
    }

    void _setRTDBState() {
        if (_databaseConfig != nullptr) {
            _databaseConfig->rtdb->set(*_databaseConfig->client, _databaseConfig->prefixPath + _rtdbPath, _state,
                                       _fbCallback, _rtdbTaskId);
        }
    }

#endif // USE_FIREBASE_RTDB

    /**
     * @brief digitalWrite wrapper
     */
    void _write();
};


#endif //SMART_GARDEN_GENERICOUTPUTBASE_H
