//
// Created by Thái Nguyễn on 24/6/24.
//

#include "Logger.h"

Logger::Logger(NTPClient *timeClient) {
    _timeClient = timeClient;
    LOG_FS.begin(true);

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
    File file = LOG_FS.open(filePath, "a");
    if (!file || file.name() == nullptr) {
        return false;
    }

    _timeClient->update();
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
        int index = line.indexOf(' ');
        if (index == -1) {
            continue;
        }

        unsigned long time = line.substring(0, index).toInt();
        if (time >= minTime) {
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



void Logger::log(String message) {
    _log(message);
}