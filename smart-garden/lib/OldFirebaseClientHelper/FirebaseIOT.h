//
// Created by Thái Nguyễn on 15/7/24.
//
#pragma once

#include <Firebase_ESP_Client.h>
#include <addons/RTDBHelper.h>
#include <cstring>
#include "base64_decode.h"
#include "GenericOutput.h"

#define DEBUG

#if defined(DEBUG)
#define D_Print(...) Serial.printf("[%d]", millis()); Serial.printf(__VA_ARGS__)
#else
#define D_Print(...)
#endif

// Firebase Objects
FirebaseData fbStream;
FirebaseData fbdo;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;

String DB_DEVICE_PATH = "";
uint32_t fb_last_millis = 0;
bool FB_FLAG_FIRST_INIT = false;
std::function<void()> OnFirstConnectCB = nullptr;


void fb_setup(const String &email, const String &password, const String &api_key, const String &database_url) {
    D_Print("Firebase setup\n");
    fbAuth.user.email = email;
    fbAuth.user.password = password;
    fbConfig.api_key = api_key;
    fbConfig.database_url = database_url;
    fbConfig.timeout.serverResponse = 10 * 1000;

    Firebase.reconnectNetwork(true);
    fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */,
                           2048 /* Tx buffer size in bytes from 512 - 16384 */);
    // Limit the size of response payload to be collected in FirebaseData
    fbdo.setResponseSize(2048);
    Firebase.begin(&fbConfig, &fbAuth);
    Firebase.setDoubleDigits(5);
    D_Print("Firebase begin\n");
}


String fb_get_uid(String token) {
    if (token.length() == 0) {
        return "";
    }
    String data = token.substring(token.indexOf('.') + 1, token.lastIndexOf('.'));
    if (data.length() == 0) {
        return "";
    }
    unsigned char *data_encoded = new unsigned char[data.length() + 1];
    std::strncpy((char*)data_encoded, data.c_str(), data.length());
    data_encoded[data.length()] = '\0';
    unsigned char data_decoded[1024];
    unsigned int data_decoded_len = decode_base64(data_encoded, data_decoded);
    delete[] data_encoded;
    if (data_decoded_len == 0) {
        return "";
    }

    D_Print("[UID DECODE] Data length: %d\n", data.length());
    D_Print("[UID DECODE] Decoded length: %d\n", data_decoded_len);

    String data_decoded_str = String((char*)data_decoded);
    int uid_index = data_decoded_str.indexOf("\"user_id\":\"");
    if (uid_index == -1)
        return "";
    return data_decoded_str.substring(uid_index + 11, data_decoded_str.indexOf("\"", uid_index + 11));
}



void fb_setStreamCallback(FirebaseData::StreamEventCallback dataAvailableCallback) {
    Firebase.RTDB.setStreamCallback(&fbStream, dataAvailableCallback, nullptr);
}

void fb_setStreamCallback(FirebaseData::StreamEventCallback dataAvailableCallback, FirebaseData::StreamTimeoutCallback timeoutCallback) {
    Firebase.RTDB.setStreamCallback(&fbStream, dataAvailableCallback, timeoutCallback);
}

void fb_onFirstConnect(std::function<void()> cb) {
    OnFirstConnectCB = std::move(cb);
}

void fb_loop() {
    // First init
    if (!FB_FLAG_FIRST_INIT && Firebase.ready()) {
        FB_FLAG_FIRST_INIT = true;

        D_Print("Firebase ready\n");

        // Set device path
        String token = Firebase.getToken();
        if (token.length() == 0) {
            D_Print("Token not found\n");
            return;
        }
        String uid = fb_get_uid(token);
        if (uid.length() == 0) {
            D_Print("UID not found\n");
            return;
        }
#if defined(ESP8266)
        DB_DEVICE_PATH = "/" + uid + "/" + String(ESP.getChipId());
#elif defined(ESP32)
        DB_DEVICE_PATH = "/" + uid + "/" + (String)ESP.getEfuseMac();
#endif
        
        D_Print("Device path: %s\n", DB_DEVICE_PATH.c_str());

        // Set stream
        if (!Firebase.RTDB.beginStream(&fbStream, DB_DEVICE_PATH + "/data")) {
            D_Print("[RTDB init] stream begin error, %s\n\n", fbStream.errorReason().c_str());
        }

        fb_last_millis = millis();

        if (OnFirstConnectCB != nullptr) {
            D_Print("[First connect callback]\n");
            OnFirstConnectCB();
        }
    }

    // Check token expired every 10 minutes
    if (FB_FLAG_FIRST_INIT && millis() - fb_last_millis > 10 * 60 * 1000) {
        fb_last_millis = millis();
        Firebase.ready();
    }
}

void fb_syncState(GenericOutput *dev, const String &path) {
    dev->onPowerChanged([dev, path]() {
        D_Print("Sync state %s - %s\n", path.c_str(), dev->getStateString().c_str());
        Firebase.RTDB.setBoolAsync(&fbdo, DB_DEVICE_PATH + "/data" + path, dev->getState());
    });
}