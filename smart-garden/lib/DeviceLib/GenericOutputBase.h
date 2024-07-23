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

// Use FS to store last state, comment this line to disable
// #define USE_LAST_STATE

#if defined(USE_LAST_STATE)
#if defined(ESP8266)
#include <LittleFS.h>
#define GO_FS LittleFS
#elif defined(ESP32)

#include <SPIFFS.h>

#define GO_FS SPIFFS
#endif
#endif // USE_LAST_STATE


//template<typename T = std::function<void()>>
//void start_callback(T cb, const char *name = "IOTask", uint32_t stack = 2048, uint8_t priority = 1) {
//    if (cb) {
//        Serial.printf("> Set callback: %s\n", name);
//        TaskHandle_t task = nullptr;
//        xTaskCreate([](void *p) {
//            auto *f = static_cast<T *>(p);
//            (*f)();
//            vTaskDelete(nullptr);
//        }, name, stack, &cb, priority, &task);
//        configASSERT(task);
//    }
//}


namespace stdGenericOutput {

    typedef enum {
        START_UP_NONE = 0xFF,
        START_UP_OFF = 0x00,
        START_UP_ON = 0x01,
        START_UP_LAST_STATE = 0x02,
    } startup_state_t;

    class GenericOutputBase;

} // stdGenericOutput



class stdGenericOutput::GenericOutputBase {

public:

    GenericOutputBase();

    /**
     * @brief Construct a new GenericOutputBase object
     *
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit GenericOutputBase(uint8_t pin, bool activeState = LOW, startup_state_t startUpState = START_UP_NONE);

#if defined(USE_PCF8574)
    /**
     * @brief Construct a new GenericOutputBase object
     *
     * @param pcf8574 PCF8574 object
     * @param pin pin number
     * @param activeState LOW or HIGH. Default is LOW
     */
    explicit GenericOutputBase(PCF8574& pcf8574, uint8_t pin, bool activeState = LOW, startup_state_t startUpState = START_UP_NONE);
#endif

    /**
     * @brief Destroy the GenericOutputBase object
     */
    ~GenericOutputBase();

    /**
     * @brief Set power to ON
     * @param force force to set power
     */
    virtual void on(bool force);

    /**
     * @brief Set power to ON
     */
    virtual void on() {
        on(false);
    }

    /**
     * @brief Set power to OFF
     * @param force force to set power
     */
    virtual void off(bool force);

    /**
     * @brief Set power to OFF
     */
    virtual void off() {
        off(false);
    }

    /**
     * @brief Toggle power
     *
     */
    void toggle();

    /**
     * @brief Set the power state
     * @param state true to turn on, false to turn off
     */
    void setState(bool state, bool force = false);

    /**
     * @brief Set the power state from string "ON" or "OFF"
     *
     * @param state
     */
    void setState(const String &state, bool force = false);

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
     * @brief Sync the state from the database to the device
     * @param state
     *
     * Called when the device is first connected to the database
     */
    void syncState(bool state) {
        if (state == _state) return;
        if (_startUpState == START_UP_LAST_STATE) {
            setState(state);
        } else {
            // set to database
            _setRTDBState();
        }
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
    uint8_t _pin{};
    bool _activeState{};
    bool _state{};
    startup_state_t _startUpState = START_UP_NONE;
    std::function<void()> _onPowerOn = nullptr;
    std::function<void()> _onPowerOff = nullptr;
    std::function<void()> _onPowerChanged = nullptr;

#ifdef USE_LAST_STATE
    const String _lastStateFSPath = "/gpiols";
#endif

    /**
     * @brief read/set last state
     */
    virtual void init();

    /**
    * @brief Read the last state. This function is called when the device is initialized
    * @return
    */
    virtual bool readLastState();

    /**
     * @brief Save to current state. This function is called when the device is turned on or off
     * Default is to save to FS. Override this function to save to other storage
     */
    virtual void setLastState();

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
            _databaseConfig->rtdb->set(
                    *_databaseConfig->client,
                    _databaseConfig->prefixPath + _rtdbPath,
                    _state,
                    _fbCallback,
                    _rtdbTaskId);
        }
    }

#endif // USE_FIREBASE_RTDB

    /**
     * @brief digitalWrite wrapper
     */
    void _write();
};


#endif //SMART_GARDEN_GENERICOUTPUTBASE_H
