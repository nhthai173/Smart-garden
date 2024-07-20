//
// Created by Thái Nguyễn on 24/6/24.
//

#include "Logger.h"

#include <utility>

Logger::Logger() {
    LOG_FS.begin();

    if (!LOG_FS.exists(filePath)) {
#if defined(ESP8266)
        File file = LOG_FS.open(filePath, "w");
        file.close();
#elif defined(ESP32)
        File file = LOG_FS.open(filePath, FILE_WRITE, true);
        file.close();
#endif
    }
}

bool Logger::_log(const String &message) {
    if (time(nullptr) <= 1609459200) {
        // Time is not set (2021-01-01)

        log_queue_item item;
        item.millis = millis();
        item.message = message;
        _log_queue.push_back(item);
        return true;
    }

    File file = LOG_FS.open(filePath, "a");
    if (!file || file.name() == nullptr) {
        return false;
    }
    time_t now;
    time(&now);
    file.printf("%ld %s\n", now, message.c_str());
    file.close();
    return true;
}


void Logger::clearOldLogs() {
    if (!LOG_FS.exists(filePath)) {
        return;
    }

    File file = LOG_FS.open(filePath, "r");
    if (!file || file.name() == nullptr) {
        return;
    }

    time_t now;
    time(&now);
    uint32_t minTime = now - _maxLogTime * 24 * 3600;

#if defined(ESP8266)
    File tempFile = LOG_FS.open("/temp_log.txt", "w");
#elif defined(ESP32)
    File tempFile = LOG_FS.open("/temp_log.txt", "w", true);
#endif
    if (!tempFile) {
        return;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() < 10) { // invalid time
            continue;
        }
        int index = line.indexOf(' ');
        if (index < 10) { // invalid time
            continue;
        }

        uint32_t time = line.substring(0, index).toInt();
        if (time >= minTime) {
            line.trim();
            tempFile.println(line);
        }
    }

    file.close();
    tempFile.close();

    LOG_FS.remove(filePath);
    LOG_FS.rename("/temp_log.txt", filePath);
}


void Logger::clearAllLogs() {
    if (!LOG_FS.exists(filePath)) {
        return;
    }

    LOG_FS.remove(filePath);
#if defined(ESP8266)
    File file = LOG_FS.open(filePath, "w");
#elif defined(ESP32)
    File file = LOG_FS.open(filePath, "w", true);
#endif

    file.close();
}


String Logger::getLogs() {
    File file = LOG_FS.open(filePath, "r");
    if (!file || file.name() == nullptr) {
        return "";
    }
    String logs = file.readString();
    file.close();
    return logs;
}


bool Logger::log(const String &message) {
    return _log(message);
}


void Logger::processQueue() {
    if (_log_queue.empty()) {
        return;
    }

    time_t now;
    time(&now);
    if (now <= 1609459200) {
        // Time is not set (2021-01-01)
        return;
    }

    File file = LOG_FS.open(filePath, "a");
    if (!file || file.name() == nullptr) {
        return;
    }

    for (auto &item: _log_queue) {
        file.printf("%ld %s\n", now - (millis() - item.millis), item.message.c_str());
    }
    _log_queue.clear();
    file.close();
}