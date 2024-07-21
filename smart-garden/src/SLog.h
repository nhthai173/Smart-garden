//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_SLOG_H
#define SMART_GARDEN_SLOG_H

#include "Logger.h"
#include "TelegramLogger.h"

class SLog : public Logger, public TelegramLogger {

private:
    String _teleLogPrefix = "";

public:
    explicit SLog(NTPClient *timeClient, FastBot *bot = nullptr) : Logger(timeClient), TelegramLogger(bot, timeClient) {
        filePath = "/slog.txt";
        if (bot != nullptr) {
            bot->setTextMode(0);
            TelegramLogger::setTimezone(7);
        }
    }

    SLog(NTPClient *timeClient, const String &bot_token, const String &chat_id = "") : Logger(timeClient) {
        filePath = "/slog.txt";
        TelegramLogger::_bot = new FastBot(bot_token);
        TelegramLogger::_bot->setTextMode(0);
        if (chat_id != "") {
            TelegramLogger::_bot->setChatID(chat_id);
        }
        TelegramLogger::setTimezone(7);
    }

    void setChatId(const String &chat_id) {
        TelegramLogger::_bot->setChatID(chat_id);
    }

    void setTeleLogPrefix(const String &prefix) {
        _teleLogPrefix = prefix;
    }

    void clearOldLogs() override {
        Logger::clearOldLogs();
    }

    void clearAllLogs() override {
        Logger::clearAllLogs();
    }

    String getLogs() override {
        return Logger::getLogs();
    }

    bool log(const String &event, const String &source, const String &message = "") {
        return Logger::log(event + "-" + source + "-" + message);
    }

    void logTele(const String &message) {
        TelegramLogger::log("*[" + _teleLogPrefix + "]*\n" + message);
    }

    void loop() {
        Logger::processQueue();
        TelegramLogger::tick();
    }

};


#endif //SMART_GARDEN_SLOG_H
