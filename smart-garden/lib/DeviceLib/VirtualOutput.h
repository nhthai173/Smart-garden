//
// Created by Thái Nguyễn on 22/6/24.
//

#ifndef VIRTUALOUTPUT_H
#define VIRTUALOUTPUT_H

#include "GenericOutput.h"

class VirtualOutput : public GenericOutput {
public:
    VirtualOutput() : GenericOutput() { }
    VirtualOutput(std::function<void()> onFunction, std::function<void()> offFunction) : GenericOutput() {
        _onFunction = std::move(onFunction);
        _offFunction = std::move(offFunction);
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
    void setOnStateString(String onStateString) {
        _onStateString = std::move(onStateString);
    }

    /**
     * @brief Set the label for OFF state
     * @param offStateString
     */
    void setOffStateString(String offStateString) {
        _offStateString = std::move(offStateString);
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
