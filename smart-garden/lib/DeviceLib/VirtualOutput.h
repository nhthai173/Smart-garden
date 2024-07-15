//
// Created by Thái Nguyễn on 22/6/24.
//

#ifndef VIRTUALOUTPUT_H
#define VIRTUALOUTPUT_H

#include "AutoOff.h"

class VirtualOutput : public AutoOff {
public:
    VirtualOutput() : AutoOff() { }

    /**
     * @brief Construct a new Virtual Output object
     * @param onFunction function to execute when power is on
     * @param offFunction function to execute when power is off
     * @param autoOffEnabled enable auto off feature
     */
    VirtualOutput(std::function<void()> onFunction, std::function<void()> offFunction, bool autoOffEnabled = false) : AutoOff() {
        _onFunction = std::move(onFunction);
        _offFunction = std::move(offFunction);
        _autoOffEnabled = autoOffEnabled;
    }

    /**
     * @brief Construct a new Virtual Output object with auto off feature and duration
     * @param autoOffEnabled enable auto off feature
     * @param duration duration to turn off after power is on in milliseconds
     */
    VirtualOutput(bool autoOffEnabled, unsigned long duration) : AutoOff() {
        _autoOffEnabled = autoOffEnabled;
        _duration = duration;
    }

    /**
     * @brief Set power to ON
     *
     */
    void on() override;

    /**
     * @brief Set power to OFF
     *
     */
    void off() override;

    /**
     * @brief alternate name for on()
     *
     */
    void open() {
        on();
    }


    /**
     * @brief set power on for a duration. Only works once
     * @param duration
     */
    void open(unsigned long duration) {
        AutoOff::on(duration);
    }

    /**
     * @brief set power on with percentage timing (0-100%) based on the duration
     * @param percentage 1-100
     */
    void openPercentage(uint8_t percentage) {
        AutoOff::onPercentage(percentage);
    }

    /**
     * @brief alternate name for off()
     *
     */
    void close() {
        off();
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

    void setOnFunction(std::function<void()> onFunction) {
        _onFunction = std::move(onFunction);
    }

    void setOffFunction(std::function<void()> offFunction) {
        _offFunction = std::move(offFunction);
    }

protected:
    String _onStateString = "ON";
    String _offStateString = "OFF";
    std::function<void()> _onFunction = nullptr;
    std::function<void()> _offFunction = nullptr;

};


#endif //VIRTUALOUTPUT_H
