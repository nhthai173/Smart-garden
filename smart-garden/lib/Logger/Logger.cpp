//
// Created by Thái Nguyễn on 24/6/24.
//

#include "Logger.h"

#include <utility>

Logger::Logger(NTPClient *timeClient) {
    _timeClient = timeClient;
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

bool Logger::_log(String message) {
    if (!_timeClient->isTimeSet()) {
        _timeClient->begin();
        _timeClient->update();
        log_item_t item = {millis(), std::move(message)};
        log_queue.push_back(item);
        return false;
    }

    File file = LOG_FS.open(filePath, "a");
    if (!file || file.name() == nullptr) {
        return false;
    }

    file.printf("%ld %s\n", _timeClient->getEpochTime(), message.c_str());
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

    _timeClient->update();
    unsigned long minTime = _timeClient->getEpochTime() - _maxLogTime * 24 * 3600;

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

        unsigned long time = line.substring(0, index).toInt();
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



bool Logger::log(String message) {
    return _log(std::move(message));
}



void Logger::processQueue() {
    if (!_timeClient->isTimeSet()) {
        return;
    }
    if (log_queue.empty()) {
        return;
    }

    String items = "";
    uint32_t now = _timeClient->getEpochTime();
    File file = LOG_FS.open(filePath, "a");
    if (!file || file.name() == nullptr) {
        return;
    }

    for (auto &item : log_queue) {
        items += String(now - round((millis() - item.time)/1000));
        items += " " + item.message + "\n";
    }
    log_queue.clear();
    file.print(items);
    file.close();
}