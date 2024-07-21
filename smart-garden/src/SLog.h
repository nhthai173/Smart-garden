//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_SLOG_H
#define SMART_GARDEN_SLOG_H

#include "Logger.h"

#define USE_TELEGRAM_LOG

#ifdef USE_TELEGRAM_LOG

#include <vector>

#define SLOG_PARSE_MODE_HTML 1
#define SLOG_PARSE_MODE_MARKDOWN 0

static const char SLOG_HTML_ENCODE_LIST[] = ">-={}().!";

class TBot {

    friend class SLog;

private:
    String _botToken;
    String _chatId;
    uint8_t _parseMode = SLOG_PARSE_MODE_MARKDOWN;
    WiFiClientSecure *_client = nullptr;

    static String encodeMarkdown(String &s) {
        if (!s.length()) return "";
        String out = "";
        for (uint16_t i = 0; i < s.length(); i++) {
            if (strchr(SLOG_HTML_ENCODE_LIST, s[i]) != nullptr) out += '\\';
            out += s[i];
        }
        return out;
    }

    static String encodeHTML(String &s) {
        if (!s.length()) return "";
        String out = "";
        for (uint16_t i = 0; i < s.length(); i++) {
            switch (s[i]) {
                case '<': out += F("&lt;"); break;
                case '>': out += F("&gt;"); break;
                case '&': out += F("&amp;"); break;
                default: out += s[i]; break;
            }
        }
        return out;
    }

    static String encodeURL(const String& s) {
        String dest = "";
        char c;
        for (uint16_t i = 0; i < s.length(); i++) {
            c = s[i];
            if (c == ' ') dest += '+';
            else if (c <= 38 || c == '+') {
                dest += '%';
                dest += (char)((c >> 4) + (((c >> 4) > 9) ? 87 : 48));
                dest += (char)((c & 0xF) + (((c & 0xF) > 9) ? 87 : 48));
            }
            else dest += c;
        }
        return dest;
    }

    String _prepareReq(const String& method, String& message = (String &) "") {
        if (!_botToken.length() || !_chatId.length()) {
            return "";
        }
        String url = "/bot" + _botToken + "/" + method;
        url += "?chat_id=" + _chatId;
        if (message.length()) {
            message = encodeURL(message);
        }
        if (_parseMode == SLOG_PARSE_MODE_MARKDOWN) {
            url += "&parse_mode=MarkdownV2";
            url += "&text=" + encodeMarkdown(message);;
        } else {
            url += "&parse_mode=HTML";
            url += "&text=" + encodeHTML(message);
        }
        return url;
    }

    static uint16_t _getMessageId(String & response) {
        if (!response.length())
            return 0;
        if (!response.startsWith("{\"ok\":true")) {
            Serial.println(response);
            return 0;
        }
        int msgIdIndex = response.indexOf("\"message_id\":");
        if (msgIdIndex == -1)
            return 0;
        int msgIdEndIndex = response.indexOf(",", msgIdIndex);
        if (msgIdEndIndex == -1)
            return 0;
        return response.substring(msgIdIndex + 13, msgIdEndIndex).toInt();
    }

public:

    TBot(const String& token, const String& chatId, uint8_t textMode = SLOG_PARSE_MODE_MARKDOWN) {
        _botToken = token;
        _chatId = chatId;
        _parseMode = textMode;
        _client = new WiFiClientSecure();
        _client->setInsecure();
    }

    ~TBot() {
        if (_client) {
            delete _client;
            _client = nullptr;
        }
    }

    uint16_t sendReq(const String& url) {
        String response;
        uint16_t httpCode = 0;

        if (!_client) {
            _client = new WiFiClientSecure();
            _client->setInsecure();
        }

        _client->connect("api.telegram.org", 443, 5000L);
        if (!_client->connected()) {
            delete _client;
            _client = new WiFiClientSecure();
            _client->setInsecure();
            _client->connect("api.telegram.org", 443, 5000L);
            if (!_client->connected()) {
                Serial.println("[Telegram] Unable to connect to telegram server");
                delete _client;
                _client = nullptr;
                return 0;
            }
        }
        _client->print(String("GET ") + url + " HTTP/1.1\r\n" +
                       "Host: api.telegram.org\r\n" +
                       "Connection: close\r\n\r\n");
        while (_client->connected()) {
            String line = _client->readStringUntil('\n');
            if (line.startsWith("HTTP/1.1")) {
                httpCode = line.substring(9, 12).toInt();
                break;
            }
            if (line == "\r") break;
        }
        while (_client->available()) {
            response += (char)_client->read();
        }
        _client->stop();

        if (httpCode > 0) {
            if (httpCode != 200) {
                Serial.printf("[Telegram] HTTP code: %d\n", httpCode);
                Serial.println(response);
                return 0;
            }
            return _getMessageId(response);
        }

        return 0;
    }

    uint16_t sendMessage(String &message) {
        String url = _prepareReq("sendMessage", message);
        if (!url.length())
            return 0;
        return sendReq(url);
    }

    uint16_t editMessage(uint16_t messageId, String &message) {
        String url = _prepareReq("editMessageText", message);
        if (!url.length())
            return 0;
        url += "&message_id=" + String(messageId);
        return sendReq(url);
    }

};

struct SLogMessage {
    String message;
    uint16_t messageId{};
    std::function<void(uint16_t)> callback;
};

#endif // USE_TELEGRAM_LOG

class SLog : public Logger{
private:
#ifdef USE_TELEGRAM_LOG
    TBot *bot = nullptr;
    std::vector<SLogMessage> _botMessageQueue;
#endif

public:

#ifdef USE_TELEGRAM_LOG

    explicit SLog(const String& token = "", const String& chatId = "") : Logger() {
        _maxLogTime = 30; // days
        filePath = "/slog.txt";
        bot = new TBot(token, chatId);
    }

    ~SLog() {
        delete bot;
    }

#else

    explicit SLog() : Logger() {
        _maxLogTime = 30; // days
        filePath = "/slog.txt";
    }

#endif

#ifdef USE_TELEGRAM_LOG
    void setBotToken(const String& token) {
        bot->_botToken = token;
    }

    void setChatId(const String& chatId) {
        bot->_chatId = chatId;
    }
#endif // USE_TELEGRAM_LOG

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

    /**
     * Log message to telegram
     * @param message
     * @param messageId if messageId > 0, edit message with this id
     */
    void logTele(const String &message, std::function<void(uint16_t)> callback = nullptr) {
#ifdef USE_TELEGRAM_LOG
        SLogMessage m;
        m.message = message;
        m.messageId = 0;
        m.callback = std::move(callback);
        _botMessageQueue.push_back(m);
#endif
    }

    void logTeleEdit(uint16_t messageId, const String &message, std::function<void(uint16_t)> callback = nullptr) {
#ifdef USE_TELEGRAM_LOG
        SLogMessage m;
        m.message = message;
        m.messageId = messageId;
        m.callback = std::move(callback);
        _botMessageQueue.push_back(m);
#endif
    }

    void loop() {
        processQueue();
#ifdef USE_TELEGRAM_LOG
        if (_botMessageQueue.empty())
            return;
        auto mes = _botMessageQueue[0];
        if (mes.messageId > 0) {
            bot->editMessage(mes.messageId, mes.message);
            if (mes.callback) {
                mes.callback(mes.messageId);
            }
        } else {
            uint16_t msgId = bot->sendMessage(mes.message);
            if (msgId) {
                _botMessageQueue[0].messageId = msgId;
            }
            if (mes.callback) {
                mes.callback(msgId);
            }
        }
        _botMessageQueue.erase(_botMessageQueue.begin());
#endif
    } // loop

};









#endif //SMART_GARDEN_SLOG_H
