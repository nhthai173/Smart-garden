//
// Created by Thái Nguyễn on 22/6/24.
//

#ifndef VIRTUALOUTPUT_H
#define VIRTUALOUTPUT_H

#include "GenericOutput.h"

class VirtualOutput : public GenericOutput {
public:
    VirtualOutput() : GenericOutput() { }

    /**
     * @brief Construct a new Virtual Output object
     * @param onFunction function to execute when power is on
     * @param offFunction function to execute when power is off
     * @param autoOffEnabled enable auto off feature
     */
    VirtualOutput(std::function<void()> onFunction, std::function<void()> offFunction, String onStateString = "ON", String offStateString = "OFF") : GenericOutput() {
        _onFunction = std::move(onFunction);
        _offFunction = std::move(offFunction);
        _onStateString = std::move(onStateString);
        _offStateString = std::move(offStateString);
    }

    /**
     * @brief Construct a new Virtual Output object with auto off feature and duration
     * @param duration duration to turn off after power is on in milliseconds
     */
    explicit VirtualOutput(uint8_t deviceId, stdGenericOutput::startup_state_t startupState, uint32_t duration = 0) : GenericOutput() {
        _pin = deviceId;
        _startUpState = startupState;
        _autoOffEnabled = false;
        _duration = duration;
        if (duration > 0) {
            _autoOffEnabled = true;
        }
    }

    ~VirtualOutput() {
        _onFunction = nullptr;
        _offFunction = nullptr;
    }

    /**
     * @brief Set the start up state
     * @param deviceId 100 - 199
     * @param startupState
     */
    void setStartUpState(uint8_t deviceId, stdGenericOutput::startup_state_t startupState) {
        _pin = deviceId;
        _startUpState = startupState;
    }

    /**
     * @brief Set power to ON
     *
     */
    void on(bool force) override;

    /**
     * @brief Set power to OFF
     *
     */
    void off(bool force) override;

    /**
     * @brief alternate name for on()
     * @param force force to set power
     */
    void open(bool force = false) {
        on(force);
    }


    /**
     * @brief set power on for a duration. Only works once, the next time it will be on with default duration
     * @param duration duration in milliseconds
     * @param force force to set power
     */
    void openOnce(uint32_t duration, bool force = false) {
        GenericOutput::onOnce(duration, force);
    }

    /**
     * @brief set power on with percentage timing (0-100%) based on the duration
     * @param percentage 1-100
     */
    void openPercentage(uint8_t percentage, bool force = false) {
        GenericOutput::onPercentage(percentage, force);
    }

    /**
     * @brief alternate name for off()
     *
     */
    void close(bool force = false) {
        off(force);
    }

    /**
     * @brief Get the state string
     * @return String
     */
    String getStateString() {
        return _state ? _onStateString : _offStateString;
    }

    /**
     * @brief Set the label for ON state
     * @param onStateString
     */
    void setOnStateString(const String& onStateString) {
        _onStateString = onStateString;
    }

    /**
     * @brief Set the label for OFF state
     * @param offStateString
     */
    void setOffStateString(const String& offStateString) {
        _offStateString = offStateString;
    }

    /**
     * @brief Set the function to execute when power is on
     * @param onFunction
     */
    void setOnFunction(std::function<void()> onFunction) {
        _onFunction = std::move(onFunction);
    }

    /**
     * @brief Set the function to execute when power is off
     * @param offFunction
     */
    void setOffFunction(std::function<void()> offFunction) {
        _offFunction = std::move(offFunction);
    }

    /**
     * @brief Set the state from last state (if used)
     */
    void init() override {
        GenericOutput::init();
        if (_state && _onFunction) {
            _onFunction();
        } else if (!_state && _offFunction) {
            _offFunction();
        }
    }

protected:
    String _onStateString = "ON";
    String _offStateString = "OFF";
    std::function<void()> _onFunction = nullptr;
    std::function<void()> _offFunction = nullptr;

};


#endif //VIRTUALOUTPUT_H
