//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_SLOG_H
#define SMART_GARDEN_SLOG_H

#include "Logger.h"

#define SLOG_DEBUG
// #define USE_TELEGRAM_LOG




// Debug macro
#ifdef SLOG_DEBUG
#define SLOG_P(...) Serial.printf(##__VA_ARGS__)
#define SLOG_Pln(s) Serial.println(s)
#else
#define SLOG_P(...)
#define SLOG_Pln(s)
#endif


// Use Telegram log macro
#ifdef USE_TELEGRAM_LOG

#include <vector>

#define SLOG_PARSE_MODE_HTML 1
#define SLOG_PARSE_MODE_MARKDOWN 0

static const char SLOG_HTML_ENCODE_LIST[] = ">-={}().!";



struct TBot_request {
    String url;
    uint16_t httpCode;
    uint16_t messageId;
    std::function<void(uint16_t)> callback;
    bool completed;
};


class TBot {

    friend class SLog;

private:
    String _botToken;
    String _chatId;
    uint8_t _parseMode = SLOG_PARSE_MODE_MARKDOWN;
    WiFiClientSecure *_client = nullptr;
    TaskHandle_t _reqHandle = nullptr;
    std::vector<TBot_request> _requestQueue;
    uint32_t _lastReqTime = 0;
    uint32_t _timeout = 6000;

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
            SLOG_Pln("[Telegram] Fail response");
            SLOG_Pln(response);
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

    void endTask() {
        if (_client) {
            delete _client;
            _client = nullptr;
        }
        if (_reqHandle) {
            _reqHandle = nullptr;
            vTaskDelete(nullptr);
        }
    }

public:

    TBot(const String& token, const String& chatId, uint8_t textMode = SLOG_PARSE_MODE_MARKDOWN) {
        _botToken = token;
        _chatId = chatId;
        _parseMode = textMode;
    }

    ~TBot() {
        if (_client) {
            delete _client;
            _client = nullptr;
        }
    }

    void sendReq(const String& url, const std::function<void(uint16_t)> &callback = nullptr) {
        if (!url.length())
            return;

        // Add to queue, process in loop
        _requestQueue.push_back({url, 0, 0, callback, false});
    }

    void sendMessage(String &message, const std::function<void(uint16_t)> &callback = nullptr) {
        String url = _prepareReq("sendMessage", message);
        if (!url.length())
            return;
        sendReq(url, callback);
    }

    void editMessage(uint16_t messageId, String &message, const std::function<void(uint16_t)> &callback = nullptr) {
        String url = _prepareReq("editMessageText", message);
        if (!url.length())
            return;
        if (messageId)
            url += "&message_id=" + String(messageId);
        sendReq(url, callback);
    }

    void process() {
        if (_requestQueue.empty())
            return;

        uint8_t cnt = 0;

        for (auto req: _requestQueue) {
            if (req.completed) {
                if (req.callback) {
                    SLOG_P("[Telegram][complete] Callback with msgId = %d\n", req.messageId);
                    req.callback(req.messageId);
                } else {
                    SLOG_P("[Telegram][complete] msgId = %d\n", req.messageId);
                }
            }
        }
        for (auto it = _requestQueue.begin(); it != _requestQueue.end();) {
            if (it->completed) {
                ++cnt;
                it = _requestQueue.erase(it);
            } else {
                ++it;
            }
        }

        SLOG_P("[Telegram][loop] Processed %d/%d requests\n", cnt, _requestQueue.size());

        if (_reqHandle != nullptr) {
            auto status = eTaskGetState(_reqHandle);
            SLOG_P("[Telegram][loop] Free space: %d, status: %d\n", uxTaskGetStackHighWaterMark(_reqHandle), status);
            return;
        }

        if (_requestQueue.empty())
            return;

        SLOG_Pln("[Telegram][loop][add task]");

        // Process in RTOS task
        xTaskCreate(
            [](void *param) {
                SLOG_Pln("[Telegram][task][processing]");
                auto *bot = (TBot *) param;

                if (!bot) {
                    SLOG_Pln("[Telegram][task] Invalid bot");
                    vTaskDelete(nullptr);
                    return;
                }

                // No task -> exit
                if (bot->_requestQueue.empty()) {
                    SLOG_Pln("[Telegram][task] No request");
                    bot->endTask();
                    return;
                }

                bot->_lastReqTime = millis();
                auto *req = &bot->_requestQueue[0];
                
                SLOG_P("[Telegram][task] Got a request\n");

                if (bot->_client) {
                    delete bot->_client;
                }

                SLOG_P("Free heap: %d\n", esp_get_free_heap_size());

                bot->_client = new WiFiClientSecure();
                bot->_client->setInsecure();
                
                SLOG_P("[Telegram][task] Sending request: ...%s\n", req->url.substring(req->url.length() - 40).c_str());

                bot->_client->connect("api.telegram.org", 443, 2000L);
                if (!bot->_client->connected()) {
                    req->completed = true;
                    SLOG_Pln("[Telegram][task] Unable to connect to telegram server");
                    return bot->endTask();
                }

                bot->_client->print(String("GET ") + req->url + " HTTP/1.1\r\n" +
                       "Host: api.telegram.org\r\n" +
                       "Connection: close\r\n\r\n");

                while (bot->_client->connected()) {
                    String line = bot->_client->readStringUntil('\n');
                    if (line.startsWith("HTTP/1.1")) {
                        req->httpCode = line.substring(9, 12).toInt();
                    }
                    if (line == "\r") break; // end header
                    if (millis() > bot->_lastReqTime + bot->_timeout) {
                        SLOG_Pln("[Telegram][task] Request timeout");
                        req->completed = true;
                        bot->_client->stop();
                        return bot->endTask();
                    }
                }

                SLOG_P("[Telegram][task] HTTP code: %d\n", req->httpCode);

                String response = "";
                while (bot->_client->available()) {
                    response += (char)bot->_client->read();
                    if (millis() > bot->_lastReqTime + bot->_timeout) {
                        SLOG_Pln("[Telegram][task] Request timeout");
                        req->completed = true;
                        bot->_client->stop();
                        return bot->endTask();
                    }
                }
                bot->_client->stop();
                if (req->httpCode > 0) {
                    if (req->httpCode != 200) {
                        SLOG_P("[Telegram][task][Error] HTTP code: %d\n", req->httpCode);
                        SLOG_Pln(response);
                    }
                    req->messageId = TBot::_getMessageId(response);
                }
                req->completed = true;
                SLOG_P("[Telegram][task] Request completed code = %d, msgId = %d\n", req->httpCode, req->messageId);
                bot->endTask();
            },
            "TBotReq",
            3072,
            this,
            1 | portPRIVILEGE_BIT,
            &_reqHandle);
    }

};

#endif // USE_TELEGRAM_LOG

class SLog : public Logger{
private:
#ifdef USE_TELEGRAM_LOG
    TBot *bot = nullptr;
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
    void logTele(const String &message, const std::function<void(uint16_t)> &callback = nullptr) {
#ifdef USE_TELEGRAM_LOG
        bot->sendMessage(const_cast<String &>(message), callback);
#endif
    }

    void logTeleEdit(uint16_t messageId, const String &message, std::function<void(uint16_t)> callback = nullptr) {
#ifdef USE_TELEGRAM_LOG
        bot->editMessage(messageId, const_cast<String &>(message), callback);
#endif
    }

    void loop() {
        processQueue();
#ifdef USE_TELEGRAM_LOG
        bot->process();
#endif
    } // loop

};









#endif //SMART_GARDEN_SLOG_H