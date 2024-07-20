//
// Created by Thái Nguyễn on 20/7/24.
//

#ifndef TIMEHELPER_H
#define TIMEHELPER_H

#include <Arduino.h>
#include <time.h>

class TimeHelper {
private:
    int8_t _timezone = 0;
    struct tm *_timeinfo = nullptr;

public:
    TimeHelper() = default;

    explicit TimeHelper(int8_t timezone, const char *ntpServer = "pool.ntp.org") {
        configTime(timezone, ntpServer);
    }

    ~TimeHelper() = default;

    void configTime(int8_t timezone = 0, const char *ntpServer = "pool.ntp.org") {
        _timezone = timezone;
        ::configTime(timezone * 3600, 0, ntpServer);
    }

    static bool isTimeSet() {
        return time(nullptr) > 1609459200; // 2021-01-01
    }

    static uint32_t getUnixTime() {
        time_t now;
        time(&now);
        return now;
    }

    String getDateTime(const char *format = "%Y-%m-%d %H:%M:%S") {
        char buffer[64];
        time_t now;
        time(&now);
        _timeinfo = localtime(&now);
        strftime(buffer, sizeof(buffer), format, _timeinfo);
        return {buffer};
    }

    String getDateTime(uint32_t timestamp, const char *format = "%Y-%m-%d %H:%M:%S") {
        char buffer[64];
        time_t raw = timestamp;
        _timeinfo = localtime(&raw);
        strftime(buffer, sizeof(buffer), format, _timeinfo);
        return {buffer};
    }

    String getISOString() {
        String str = getDateTime("%Y-%m-%dT%H:%M:%S");
        if (_timezone == 0) {
            str += "Z";
        } else {
            str += _timezone > 0 ? "+" : "-";
            str += String(_timezone);
            str += ":00";
        }
        return str;
    };
};

#endif //TIMEHELPER_H
