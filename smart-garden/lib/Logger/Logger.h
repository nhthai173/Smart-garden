//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_LOGGER_H
#define SMART_GARDEN_LOGGER_H

#include <Arduino.h>
#include <vector>
#include <NTPClient.h>
#if defined(ESP8266)
#include <LittleFS.h>
#define LOG_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define LOG_FS SPIFFS
#endif

typedef struct {
    uint32_t time;
    String message;
} log_item_t;

class Logger {

public:

    Logger(NTPClient *timeClient);

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

    virtual bool log(String message);

    virtual String getLogs();

    void processQueue();

protected:
    String filePath = "/logs.txt";
    NTPClient* _timeClient;
    uint16_t _maxLogTime = 365; // days
    std::vector<log_item_t> log_queue;

    bool _log(String message);
};


#endif //SMART_GARDEN_LOGGER_H
