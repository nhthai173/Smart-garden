//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_LOGGER_H
#define SMART_GARDEN_LOGGER_H

#include <Arduino.h>
#include <vector>
#if defined(ESP8266)
#include <LittleFS.h>
#define LOG_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define LOG_FS SPIFFS
#endif

class Logger {

public:

    struct log_queue_item {
        uint32_t millis;
        String message;
    };

    Logger();

    /**
     * Set maximum log time in days
     * @param days
     */
    void setMaxLogTime(uint16_t days) {
        _maxLogTime = days;
    }

    /**
     * Get maximum log time in days
     * @return
     */
    uint16_t getMaxLogTime() const {
        return _maxLogTime;
    }

    virtual void clearOldLogs();

    virtual void clearAllLogs();

    virtual bool log(const String& message);

    virtual String getLogs();

    void processQueue();

protected:
    String filePath = "/logs.txt";
    uint16_t _maxLogTime = 365; // days

    std::vector<log_queue_item> _log_queue;

    bool _log(const String& message);
};


#endif //SMART_GARDEN_LOGGER_H
