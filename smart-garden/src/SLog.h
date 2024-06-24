//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_SLOG_H
#define SMART_GARDEN_SLOG_H

#include "Logger.h"

class SLog : public Logger {

public:
    explicit SLog(NTPClient *timeClient) : Logger(timeClient) {
    }

    bool log(const String& event, const String& source, const String& message) {
        return Logger::log(event + "-" + source + "-" + message);
    }

};

#endif //SMART_GARDEN_SLOG_H
