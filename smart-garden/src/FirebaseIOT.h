//
// Created by Thái Nguyễn on 18/7/24.
//

#ifndef SMART_GARDEN_FIREBASEIOT_H
#define SMART_GARDEN_FIREBASEIOT_H

#define USE_STREAM
#define USE_OTA
#define FB_PRINT_RESULT

#include <FirebaseClient.h>
#include <WiFiClientSecure.h>
#include "json_parser.h"

DefaultNetwork fbNetwork; // initilize with boolean parameter to enable/disable network reconnection
UserAuth *user_auth;
FirebaseApp app;
WiFiClientSecure ssl_client;
AsyncClientClass aClient(ssl_client, getNetwork(fbNetwork));

RealtimeDatabase Database;
AsyncResult aResult_no_callback;

#ifdef USE_STREAM

WiFiClientSecure stream_ssl_client;
AsyncClientClass streamClient(stream_ssl_client, getNetwork(fbNetwork));

#endif // USE_STREAM

#ifdef FB_PRINT_RESULT

void printResult(AsyncResult &aResult) {
    if (aResult.isEvent()) {
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(),
                        aResult.appEvent().message().c_str(), aResult.appEvent().code());
    }

    if (aResult.isDebug()) {
        Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
    }

    if (aResult.isError()) {
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(),
                        aResult.error().code());
    }

    if (aResult.available()) {
        auto &RTDB = aResult.to<RealtimeDatabaseResult>();
        if (RTDB.isStream()) {
            Serial.println("----------------------------");
            Firebase.printf("task: %s\n", aResult.uid().c_str());
            Firebase.printf("event: %s\n", RTDB.event().c_str());
            Firebase.printf("path: %s\n", RTDB.dataPath().c_str());
            Firebase.printf("data: %s\n", RTDB.to<const char *>());
            Firebase.printf("type: %d\n", RTDB.type());
        } else {
            Serial.println("----------------------------");
            Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
        }
    }
}

#else

void printResult(AsyncResult &aResult) {}

#endif // FB_PRINT_RESULT


class FirebaseIOTClass {

private:
    bool _firstConnected = false;
    std::function<void()> _firstConnectedCB = nullptr;

public:
    String DB_DEVICE_PATH;

    static void begin(const String &api, const String &db_url, const String &email, const String &password) {
        ssl_client.setInsecure();

#ifdef USE_STREAM
        stream_ssl_client.setInsecure();
#endif

        user_auth = new UserAuth(api, email, password);

        initializeApp(aClient, app, getAuth(*user_auth), printResult, "authTask");

        // Binding the FirebaseApp for authentication handler.
        // To unbind, use Database.resetApp();
        app.getApp<RealtimeDatabase>(Database);
        Database.url(db_url);

        Database.setSSEFilters("get,put,patch,cancel");
//    Database.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
    }

    void onFirstConnected(std::function<void()> cb) {
        _firstConnectedCB = std::move(cb);
    }

    void loop() {
        app.loop();
        Database.loop();

        if (app.ready() && !_firstConnected) {
            _firstConnected = true;

            DB_DEVICE_PATH = app.getUid() + "/" + (String) ESP.getEfuseMac();

            if (_firstConnectedCB) {
                _firstConnectedCB();
            }
        }
    }

    template<typename T = object_t>
    void set(const String &path, const T &value) {
        Database.set<T>(aClient, DB_DEVICE_PATH + path, value, aResult_no_callback);
    }

    template<typename T = object_t>
    void set(const String &path, const T &value, AsyncResultCallback callback, const String &uid = "") {
        Database.set<T>(aClient, DB_DEVICE_PATH + path, value, callback, uid);
    }

    template<typename T = object_t>
    void update(const String &path, const T &value) {
        Database.update<T>(aClient, DB_DEVICE_PATH + path, value, aResult_no_callback);
    }

    template<typename T = object_t>
    void update(const String &path, const T &value, AsyncResultCallback callback, const String &uid = "") {
        Database.update<T>(aClient, DB_DEVICE_PATH + path, value, callback, uid);
    }

    void get(const String &path, AsyncResultCallback callback, const String &uid = "") const {
        Database.get(aClient, DB_DEVICE_PATH + path, callback, false, uid);
    }

    void remove(const String &path) const {
        Database.remove(aClient, DB_DEVICE_PATH + path, aResult_no_callback);
    }

    void remove(const String &path, AsyncResultCallback callback, const String &uid = "") const {
        Database.remove(aClient, DB_DEVICE_PATH + path, callback, uid);
    }

#ifdef USE_STREAM

    bool setStream(const String &path, AsyncResultCallback callback, const String &uid = "streamTask") const {
        Database.get(streamClient, DB_DEVICE_PATH + path, callback, true, uid);
        return true;
    }

#endif

#if defined(USE_OTA)

    /**
     * @brief Download firmware from Firebase RTDB
     * @param path
     * @param callback
     * @param uid
     */
    void beginOTA(const String &path, AsyncResultCallback callback, const String &uid = "otaTask") const {
        Database.ota(aClient, DB_DEVICE_PATH + path, callback, uid);
    }

#endif

};


static FirebaseIOTClass FirebaseIOT;

#endif //SMART_GARDEN_FIREBASEIOT_H
