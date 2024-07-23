//
// Created by Thái Nguyễn on 18/7/24.
//

#include "GenericOutputBase.h"

stdGenericOutput::GenericOutputBase::GenericOutputBase() {
#ifdef USE_LAST_STATE
    GO_FS.begin(true);
#endif
    init();
}

stdGenericOutput::GenericOutputBase::GenericOutputBase(uint8_t pin, bool activeState, startup_state_t startUpState) {
#ifdef USE_LAST_STATE
    GO_FS.begin(true);
#endif

    _pin = pin;
    _activeState = activeState;
    pinMode(_pin, OUTPUT);
    _startUpState = startUpState;
    init();
}

#ifdef USE_PCF8574
stdGenericOutput::GenericOutputBase::GenericOutputBase(PCF8574& pcf8574, uint8_t pin, bool activeState) {
#ifdef USE_LAST_STATE
    GO_FS.begin(true);
#endif // USE_LAST_STATE

    _pin = pin;
    _activeState = activeState;
    _pcf8574 = &pcf8574;
    pcf8574.pinMode(_pin, OUTPUT);
    init();
}
#endif

stdGenericOutput::GenericOutputBase::~GenericOutputBase() {
    _onPowerChanged = nullptr;
    _onPowerOff = nullptr;
    _onPowerOn = nullptr;

#if defined(USE_FIREBASE_RTDB)
    _databaseConfig = nullptr;
    _fbCallback = nullptr;
#endif

}

bool stdGenericOutput::GenericOutputBase::readLastState() {
#ifdef USE_LAST_STATE
    if (GO_FS.exists(_lastStateFSPath)) {
        File file = GO_FS.open(_lastStateFSPath, "r");
        if (file) {
            String pinName = String(_pin) + ":";
            while (file.available()) {
                String line = file.readStringUntil('\n');
                if (line.startsWith(pinName)) {
                    file.close();
                    return line.substring(pinName.length()).toInt() == 1;
                }
            }
            file.close();
        }
    }
    _state = false;
    setLastState();
    return false;
#else
    return false;
#endif
}

void stdGenericOutput::GenericOutputBase::setLastState() {
#ifdef USE_LAST_STATE
    if (_startUpState == START_UP_NONE) {
        return; // Do not save state if start up state is not set
    }

    Serial.printf("Set last state of [%d] to %d\n", _pin, _state);
    String pinName = String(_pin) + ":";

#ifdef ESP8266
    File file = GO_FS.open(_lastStateFSPath, "r+");
    if (file) {
        bool found = false;
        while (file.size() && file.available()) {
            String line = file.readStringUntil('\n');
            if (line.startsWith(pinName)) {
                Serial.printf("> Found at %d\n", file.position() - line.length() - 1);
                found = true;
                file.seek(file.position() - line.length() - 1);
                file.print(pinName + (_state ? "1" : "0"));
                break;
            }
        }
        if (!found) {
            Serial.printf("> Not found, print to new line\n");
            file.println(pinName + (_state ? "1" : "0"));
        }
        file.close();
    } else {
        Serial.println("> Fail to open file");
    }
#else // ESP32
    File file = GO_FS.open(_lastStateFSPath, "r");
    if (file) {
        int index = -1;
        String fileContent = file.readString();
        file.close();
        if (fileContent.length()) {
            index = fileContent.indexOf(pinName);
        }
        if (index >= 0) {
            fileContent = fileContent.substring(0, index) + pinName + (_state ? "1" : "0") +
                          fileContent.substring(index + pinName.length() + 1);
            file = GO_FS.open(_lastStateFSPath, "w", true);
            if (file) {
                file.print(fileContent);
                file.close();
            } else {
                Serial.println("> Fail to open file");
            }
        } else {
            file = GO_FS.open(_lastStateFSPath, "a");
            if (file) {
                file.println(pinName + (_state ? "1" : "0"));
                file.close();
            } else {
                Serial.println("> Fail to open file");
            }
        }
    } else {
        file = GO_FS.open(_lastStateFSPath, "w", true);
        if (file) {
            file.println(pinName + (_state ? "1" : "0"));
            file.close();
        } else {
            Serial.println("> Fail to open file");
        }
    }
#endif
#endif // USE_LAST_STATE
}

void stdGenericOutput::GenericOutputBase::init() {
    if (_startUpState != START_UP_NONE) {
        if (_startUpState == START_UP_ON) {
            _state = true;
        } else if (_startUpState == START_UP_OFF) {
            _state = false;
        } else if (_startUpState == START_UP_LAST_STATE) {
            _state = readLastState();
        }
    } else {
        _state = false;
    }
    _write();
}

void stdGenericOutput::GenericOutputBase::_write() {
#if defined(USE_PCF8574)
    if (_pcf8574 != nullptr) {
        _pcf8574->digitalWrite(_pin, _state ? _activeState : !_activeState);
    } else {
        digitalWrite(_pin, _state ? _activeState : !_activeState);
    }
#else // Hardware pin
#ifdef NUM_DIGITAL_PINS
    if (_pin >= NUM_DIGITAL_PINS) return; // Invalid pin
#endif
    digitalWrite(_pin, _state ? _activeState : !_activeState);
#endif // USE_PCF8574

#ifdef USE_LAST_STATE
    setLastState();
#endif

} // _write

void stdGenericOutput::GenericOutputBase::on(bool force) {
    if (!force && _state) return;
    _state = true;
    _write();
    if (_onPowerOn != nullptr) {
        Serial.printf("> P[%d] -> Pon\n", _pin);
        _onPowerOn();
//        start_callback(_onPowerOn, String("pon_" + String(_pin)).c_str());
    }
    if (_onPowerChanged != nullptr) {
        Serial.printf("> P[%d] -> Pchange\n", _pin);
        _onPowerChanged();
//        start_callback(_onPowerChanged, String("pchange_" + String(_pin)).c_str());
    }
#if defined(USE_FIREBASE_RTDB)
    if (_rtdbPath.length() > 0) {
        _setRTDBState();
    }
#endif
}

void stdGenericOutput::GenericOutputBase::off(bool force) {
    if (!force && !_state) return;
    _state = false;
    _write();
    if (_onPowerOff != nullptr) {
        Serial.printf("> P[%d] -> Poff\n", _pin);
        _onPowerOff();
//        start_callback(_onPowerOff, String("poff_" + String(_pin)).c_str());
    }
    if (_onPowerChanged != nullptr) {
        Serial.printf("> P[%d] -> Pchange\n", _pin);
        _onPowerChanged();
//        start_callback(_onPowerChanged, String("pchange_" + String(_pin)).c_str());
    }
#if defined(USE_FIREBASE_RTDB)
    if (_rtdbPath.length() > 0) {
        _setRTDBState();
    }
#endif
}

void stdGenericOutput::GenericOutputBase::toggle() {
    if (_state) {
        off();
    } else {
        on();
    }
}

void stdGenericOutput::GenericOutputBase::setState(bool state, bool force) {
    if (state) {
        on(force);
    } else {
        off(force);
    }
}

void stdGenericOutput::GenericOutputBase::setState(const String &state, bool force) {
    String s = state;
    s.toUpperCase();
    if (s.startsWith("ON")) {
        on(force);
    } else if (s.startsWith("OFF")) {
        off(force);
    }
}

void stdGenericOutput::GenericOutputBase::setActiveState(bool activeState) {
    _activeState = activeState;
}

void stdGenericOutput::GenericOutputBase::setStartUpState(startup_state_t startUpState) {
    _startUpState = startUpState;
}

bool stdGenericOutput::GenericOutputBase::getActiveState() const {
    return _activeState;
}

bool stdGenericOutput::GenericOutputBase::getState() const {
    return _state;
}

String stdGenericOutput::GenericOutputBase::getStateString() const {
    return _state ? "ON" : "OFF";
}

stdGenericOutput::startup_state_t stdGenericOutput::GenericOutputBase::getStartUpState() const {
    return _startUpState;
}