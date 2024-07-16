#ifndef TELEGRAM_LOGGER_H
#define TELEGRAM_LOGGER_H

#include "TelegramBotLite.h"
#include <vector>
#include "WiFiUdp.h"
#include "NTPClient.h"

typedef struct {
    uint32_t unix = 0;
    bool isUnix = true;
    String message = "";
    String urlText = "";
    String url = "";
} TL_item;

class TelegramLogger {
public:
    TelegramLogger() = default;

    explicit TelegramLogger(TelegramBotLite *bot, NTPClient *ntpClient = nullptr) {
        _bot = bot;
        _ntpClient = ntpClient;
        if (_bot != nullptr) {
            _bot->setTextMode(0);
        }
        begin();
    }

    void setTimezone(uint8_t timezone) {
        _ntpClient->setTimeOffset(timezone * 3600);
    }

    void begin() {
        if (_ntpClient == nullptr) {
            WiFiUDP *udp = new WiFiUDP();
            _ntpClient = new NTPClient(*udp, "pool.ntp.org");
        }
        _ntpClient->begin();
    }

    void log(const String &message, const String &urlText = "", const String &url = "") {
        if (!_ntpClient->isTimeSet()) {
            log(millis(), message, urlText, url, false);
            return;
        }
        log(_ntpClient->getEpochTime(), message, urlText, url, true);
    }

    void
    log(uint32_t unix, const String &message, const String &urlText = "", const String &url = "", bool isUnix = true) {
        TL_item item;
        item.unix = unix;
        item.isUnix = isUnix;
        item.message = message;
        item.urlText = urlText;
        item.url = url;
        if (url != "") {
            if (!url.startsWith("http")) {
                item.url = "https://" + url;
            }
        }
        log_queue.push_back(item);
    }

    String parseDate(uint32_t unix) {
        return _ntpClient->getFormattedDateTime("dd/MM/yyyy HH:mm:ss", unix);
    }

    void tick() {
        if (log_queue.empty())
            return;
        TL_item item = log_queue[0];
        if (item.unix > 0 && item.message != "") {
            if (!_ntpClient->isTimeSet()) {
                _ntpClient->update();
                return;
            }

            uint32_t unix = item.unix;
            if (!item.isUnix) {
                unix = _ntpClient->getEpochTime() - round((millis() - item.unix) / 1000);
            }
            if (item.url != "") {
                _bot->inlineMenuCallback("*" + parseDate(unix) + "*\n" + item.message, item.urlText, item.url);
            } else {
                _bot->sendMessage("*" + parseDate(unix) + "*\n" + item.message);
            }
        }
        log_queue.erase(log_queue.begin());
    }

protected:
    TelegramBotLite *_bot = nullptr;
    NTPClient *_ntpClient = nullptr;
    std::vector<TL_item> log_queue;
};

#endif // TELEGRAM_LOGGER_H