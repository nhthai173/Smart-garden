//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_SLOG_H
#define SMART_GARDEN_SLOG_H

#include "Logger.h"

class SLog : public Logger {

public:
    explicit SLog(NTPClient *timeClient) : Logger(timeClient) {
        filePath = "/slog.txt";
    }

    void clearOldLogs() override {
        Logger::clearOldLogs();
    }

    void clearAllLogs() override {
        Logger::clearAllLogs();
    }

    bool log(const String& message) {
        return Logger::log(message);
    }

    String getLogs() override {
        return Logger::getLogs();
    }

    bool log(const String& event, const String& source, const String& message = "") {
        return log(event + "-" + source + "-" + message);
    }

};

#endif //SMART_GARDEN_SLOG_H
