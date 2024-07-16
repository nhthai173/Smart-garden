//
// Created by Thái Nguyễn on 16/7/24.
//

#ifndef SMART_GARDEN_TELEGRAMBOTLITE_H
#define SMART_GARDEN_TELEGRAMBOTLITE_H
#ifdef ESP32

#include "WiFi.h"
#include "HTTPClient.h"

class TelegramBotLite {
private:
    String _token;

protected:
    HTTPClient* _http;
    String _chat_id;

public:
    TelegramBotLite(const String& token, const String& chat_id = "") {
        _http = new HTTPClient();
    }

    void setTextMode(uint8_t mode) {

    }

    void setChatId(const String& chat_id) {
        _chat_id = chat_id;
    }

    void sendMessage(const String& message) {

    }

    void sendMessage(const String& chat_id, const String& message) {

    }

    void inlineMenuCallback(const String& message, const String& menu, const String& callback) {

    }

};


#endif //ESP32
#endif //SMART_GARDEN_TELEGRAMBOTLITE_H
