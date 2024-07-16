/**
 * @file main.cpp
 * @author Thai Nguyen (nhthai173@gmail.com)
 * @brief 
 * @version 1.0
 * @date 2024-07-16
 * 
 * @copyright Copyright nht (c) 2024
 * 
 */

/**
 *
 */

// #define ENABLE_SERVER
#define ENABLE_FIREBASE
// #define ENABLE_NTP


#include <Arduino.h>
#include <WiFi.h>
#include <SimpleTimer.h>

#if defined(ENABLE_NTP)
#include <WiFiUdp.h>
#include <NTPClient.h>

#include "WateringSchedule.h"
#include "SLog.h"
#endif

#if defined(ENABLE_SERVER)

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include "MAIN_PAGE.h"

#endif

#if defined(ENABLE_FIREBASE)

#include "FirebaseIOT.h"

#endif

#include "GenericOutput.h"
#include "GenericInput.h"
#include "VirtualOutput.h"
#include "VoltageReader.h"
#include "AutoOff.h"

#include "secret.h"

#define DEVICE_NAME "Watering System"
#define DEVICE_VERSION "0.3.0"
#define FIRMWARE_VERSION 34

#define FLOW_SENSOR_PIN 35
#define VOLTAGE_PIN 32

#define OUTPUT_ACTIVE_STATE LOW

AutoOff ValvePower(19, OUTPUT_ACTIVE_STATE, 8000L /* 8 sec */); // R1
GenericOutput ValveDirection(18, OUTPUT_ACTIVE_STATE); // R2
AutoOff PumpPower(16, OUTPUT_ACTIVE_STATE); // R3
AutoOff ACPower(4, OUTPUT_ACTIVE_STATE, 180000L /* 3 min */); // R4
VirtualOutput Valve(true, 60000L /* 1 min */);
GenericInput WaterLeak(34, INPUT_PULLUP, LOW);
VoltageReader PowerVoltage(32, 10.0, 3.33, 0.3, 10.5, 13.5);

GenericOutput led(LED_BUILTIN, OUTPUT_ACTIVE_STATE);

SimpleTimer timer;

#if defined(ENABLE_NTP)
WiFiUDP ntpUDP;
NTPClient timeClient = NTPClient(ntpUDP, "pool.ntp.org", 7 * 3600);

Scheduler<WateringTaskArgs> scheduler(&timeClient);
//SLog logger(&timeClient, BOT_TOKEN, CHAT_ID);
SLog logger(&timeClient);
#endif


#if defined(ENABLE_SERVER)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
#endif


/**
 * @brief Connect known WiFi network
 *
 * @return true
 * @return false
 */
bool connectWiFi();


/**
 * @brief Notify all clients of the current state
 *
 * @param server
 */
void notifyState();

#if defined(ENABLE_SERVER)

/**
 * @brief WebSocket event handler
 *
 * @param sv
 * @param client
 * @param type
 * @param arg
 * @param data
 * @param len
 */
void
WSHandler(AsyncWebSocket *sv, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    String message = (char *) data;

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client [%d] connected\n", client->id());
            notifyState();
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WS client [%d] disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            Serial.printf("WS client [%d] message: %s\n", client->id(), message.c_str());
            if (message.startsWith("R1:")) {
                ValvePower.setState(message.substring(3));
            } else if (message.startsWith("R2:")) {
                ValveDirection.setState(message.substring(3));
            } else if (message.startsWith("R3:")) {
                PumpPower.setState(message.substring(3));
            } else if (message.startsWith("R4:")) {
                ACPower.setState(message.substring(3));
            } else if (message.startsWith("VALVE:")) {
                if (message.substring(6).startsWith("OPEN")) {
                    Valve.open();
                } else {
                    Valve.close();
                }
                logger.log("VALVE_" + message.substring(6), "WEB", client->remoteIP().toString());
            }
            break;
        case WS_EVT_PONG:
            break;
        case WS_EVT_ERROR:
            break;
        default:
            break;
    }
}

#endif


#if defined(ENABLE_FIREBASE)

/**
 * @brief Firebase stream callback
 * @param data
 */
void streamCallback(FirebaseStream data) {
    Serial.printf("sream path, %s\nevent path, %s\ndata type, %s\nevent type, %s\n\n",
                  data.streamPath().c_str(),
                  data.dataPath().c_str(),
                  data.dataType().c_str(),
                  data.eventType().c_str());

//    if (data.dataPath() == "/") {
//        FirebaseJsonData jdata;
//        data.jsonObjectPtr()->get(jdata, "R1");
//        if (jdata.success) {
//            R1.setState(jdata.boolValue);
//        }
//        data.jsonObjectPtr()->get(jdata, "R2");
//        if (jdata.success) {
//            R2.setState(jdata.boolValue);
//        }
//        data.jsonObjectPtr()->get(jdata, "R3");
//        if (jdata.success) {
//            R3.setState(jdata.boolValue);
//        }
//    } else if (data.dataPath() == "/R1") {
//        R1.setState(data.boolData());
//    } else if (data.dataPath() == "/R2") {
//        R2.setState(data.boolData());
//    } else if (data.dataPath() == "/R3") {
//        R3.setState(data.boolData());
//    }

    printResult(data); // see addons/RTDBHelper.h
    Serial.println();
    Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());
}

#endif


void mainLoop() {
    // ValvePower.loop();
    // ACPower.loop();
    // Valve.loop();
    // PumpPower.loop();
    // PowerVoltage.loop();
#if defined(ENABLE_NTP)
    timeClient.update();
    scheduler.run();
    logger.loop();
#endif
}

#if defined(ENABLE_NTP)
/**
 * Set a test schedule
 * @return
 */
String setTestSchedule(uint8_t id, uint8_t hour, uint8_t min) {
    if (scheduler.getTaskById(id).id) return "Schedule already set";
    schedule_task_t<WateringTaskArgs> task = {
            .id = id,
            .time = {
                    .hour = hour,
                    .minute = min
            },
            .repeat = {
                    .monday = true,
                    .tuesday = true,
                    .wednesday = true,
                    .thursday = true,
                    .friday = true,
                    .saturday = true,
                    .sunday = true
            },
            .args = new WateringTaskArgs(),
            .enabled = true,
            .executed = false
    };
    return scheduler.addTask(task) ? "Schedule set" : "Failed to set schedule";
}

String removeSchedule(uint8_t id) {
    return scheduler.removeTask(id) ? "Schedule removed" : "Failed to remove schedule";
}

#endif // ENABLE_NTP


#if defined(ENABLE_NTP)
/**
 * @brief Execute a watering task from schedule
 *
 * @param task
 */
void WateringTaskExec(schedule_task_t<WateringTaskArgs>);
#endif

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000)
        delay(10);

    if (!connectWiFi()) {
        Serial.println("Failed to connect to WiFi");
    }

//    logger.setTeleLogPrefix("🌱 Vườn cây");

    timer.setInterval(1000L, []() {
        led.toggle();
    });

    ACPower.onPowerOn([]() {
        delay(1000); // wait for power to stabilize
    });
    ACPower.onPowerOff([]() {
        ValvePower.off();
        PumpPower.off();
    });
    ACPower.onPowerChanged(notifyState);

    ValvePower.onPowerOff([]() {
        ValveDirection.off();
    });
    ValvePower.onPowerChanged(notifyState);

    Valve.setOnStateString("OPEN");
    Valve.setOffStateString("CLOSE");

    Valve.setOnFunction([]() {
        ValveDirection.off();
        ACPower.on();
        ValvePower.on();
        PumpPower.on();
    });
    Valve.setOffFunction([]() {
        ValveDirection.on();
        ACPower.on();
        ValvePower.on();
        PumpPower.off();
    });
    Valve.onPowerChanged(notifyState);
    Valve.onAutoOff([]() {
#if defined(ENABLE_NTP)
        logger.log("VALVE_CLOSE", "AUTO_TIMEOUT", "");
#endif
    });

    PumpPower.setPowerOnDelay(8000L);
    PumpPower.onPowerChanged(notifyState);

    ValveDirection.onPowerChanged(notifyState);

    WaterLeak.setActiveStateString("LEAK");
    WaterLeak.setInactiveStateString("NONE");
    WaterLeak.onChange([]() {
        notifyState();
    });
    WaterLeak.onHoldState(true, 5000L, []() {
        Valve.close();
#if defined(ENABLE_NTP)
        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
        logger.log("VALVE_CLOSE", "WATER_LEAK", "");
        logger.logTele("🚨 Phát hiện ngập nước");
#endif
    });
    WaterLeak.onHoldState(false, 5000L, []() {
#if defined(ENABLE_NTP)
        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
        logger.logTele("🚰 Nước đã hết ngập");
#endif
    });

#if defined(ENABLE_NTP)
    scheduler.setCallback(WateringTaskExec);
#endif

#if defined(ENABLE_SERVER)
    server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAINPAGE); });

    server.on("/info", HTTP_ANY, [](AsyncWebServerRequest *request) {
        String message = "{";
        message += R"("device":")" DEVICE_NAME "\",";
        message += R"("version":")" DEVICE_VERSION "\",";
        message += R"("firmware":)" + String(FIRMWARE_VERSION) + ",";
        message += R"("ip":")" + WiFi.localIP().toString() + "\",";
        message += R"("voltage":)" + String(PowerVoltage.get()) + ",";
        message += R"("valve":")" + Valve.getStateString() + "\",";
        message += R"("r1":")" + ValvePower.getStateString() + "\",";
        message += R"("r2":")" + ValveDirection.getStateString() + "\",";
        message += R"("r3":")" + PumpPower.getStateString() + "\",";
        message += R"("r4":")" + ACPower.getStateString() + "\",";
        message += R"("r1_auto_off":)" + String(ValvePower.getDuration()) + ",";
        message += R"("r4_auto_off":)" + String(ACPower.getDuration()) + ",";
        message += R"("valve":")" + Valve.getStateString() + "\",";
        message += R"("valve_auto_off":)" + String(Valve.getDuration()) + ",";
        message += R"("water_leak":")" + WaterLeak.getStateString() + "\"";
        message += "}";
        request->send(200, "application/json", message);
    });


    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(1000);
        ESP.restart();
    });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Reseting...");
        SPIFFS.format();
        SPIFFS.end();
        ESP.restart();
    });


    /**
     * Endpoint /watering
     * @param stop - stop watering
     */
    server.on("/watering", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (request->hasParam("stop")) {
            Valve.close();
            logger.log("VALVE_CLOSE", "API", request->client()->remoteIP().toString());
        } else {
            Valve.open();
            logger.log("VALVE_OPEN", "API", request->client()->remoteIP().toString());
        }
        request->send(200, "text/plain", "OK");
    });


    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", logger.getLogs());
    });

    server.on("/schedules", HTTP_GET, [](AsyncWebServerRequest *request) {
        String ret = "Object task count: " + String(scheduler.getTaskCount()) + "\n";
        ret += "Read from file:\n" + scheduler.getString();
        request->send(200, "text/plain", ret);
    });

    server.on("/test-schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t id = 0, hour, min;
        if (request->hasParam("id")) {
            id = request->getParam("id")->value().toInt();
        }
        if (request->hasParam("hour")) {
            hour = request->getParam("hour")->value().toInt();
        }
        if (request->hasParam("minute")) {
            min = request->getParam("minute")->value().toInt();
        }
        if (!id) {
            request->send(400, "text/plain", "Invalid id");
            return;
        }
        request->send(200, "text/plain", setTestSchedule(id, hour, min));
    });

    server.on("/remove-schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t id = 0;
        if (request->hasParam("id")) {
            id = request->getParam("id")->value().toInt();
        }
        if (!id) {
            request->send(400, "text/plain", "Invalid id");
            return;
        }
        request->send(200, "text/plain", removeSchedule(id));
    });

    ElegantOTA.onEnd([](bool isSuccess) {
        if (isSuccess) {
            logger.log("FIRMWARE_UPDATE", "WEB", String(FIRMWARE_VERSION));
        } else {
            logger.log("FIRMWARE_UPDATE_FAILED", "WEB", String(FIRMWARE_VERSION));
        }
    });

    /* ===================== */

    ws.onEvent(WSHandler);
    ElegantOTA.begin(&server);
    server.addHandler(&ws);
    server.begin();
#endif

#if defined(ENABLE_NTP)
    timeClient.begin();
    timeClient.update();

    logger.clearOldLogs();
    logger.log("START", "SYSTEM", String(FIRMWARE_VERSION));
#endif

    timer.setInterval(500L, mainLoop);

#if defined(ENABLE_FIREBASE)
    // Firebase setup
    fb_onFirstConnect([]() {
        D_Print("Set name\n");
        if (Firebase.RTDB.setInt(&fbdo, DB_DEVICE_PATH + "/name", rand())) {
            D_Print("Set name success\n");
        } else {
            D_Print("Set name failed\n");
            D_Print("Error: %s\n", fbdo.errorReason().c_str());
        }

        FirebaseJson json;
        json.add("name", DEVICE_NAME);
        json.add("firmware", FIRMWARE_VERSION);
        json.add("version", DEVICE_VERSION);
        json.add("ip", WiFi.localIP().toString());
        D_Print("-> Update device info\n");
        if (Firebase.RTDB.set(&fbdo, DB_DEVICE_PATH + "/info", &json)) {
            D_Print("Update info success\n");
        } else {
            D_Print("Update info failed\n");
            D_Print("Error: %s\n", fbdo.errorReason().c_str());
        }

       if (Firebase.RTDB.getJSON(&fbdo, DB_DEVICE_PATH + "/data")) {
           FirebaseJsonData data = fbdo.jsonData();
           D_Print("Get /data: %s", data.stringValue.c_str());
           D_Print("Type: %s", data.type.c_str());

           // Device first time connect
           // @TODO implement FireBaseDevice class
           if (data.type ==  "null") {
               D_Print("Not found /data, set default state\n");
               FirebaseJson jsonData;
               jsonData.add("valve", Valve.getState());
               jsonData.add("r1", ValvePower.getState());
               jsonData.add("r2", ValveDirection.getState());
               jsonData.add("r3", PumpPower.getState());
               jsonData.add("r4", ACPower.getState());
               jsonData.add("s1", WaterLeak.getStateString());
               if (Firebase.RTDB.set(&fbdo, DB_DEVICE_PATH + "/data", &jsonData)) {
                   D_Print("Set default state success\n");
               } else {
                   D_Print("Set default state failed\n");
                   D_Print("Error: %s\n", fbdo.errorReason().c_str());
               }
               return;
           }

           // Sync state
           // @TODO implement last state mode on GeneralOutput and server
       } else {
           D_Print("Failed to get /data\n");
           D_Print("Error: %s\n", fbdo.errorReason().c_str());
       }

    });

    fb_setup(USER_EMAIL,
             USER_PASSWORD,
             API_KEY,
             DATABASE_URL);
    fb_setStreamCallback(streamCallback);
#endif // ENABLE_FIREBASE

} // setup





void loop() {
    // WaterLeak.loop();
    timer.run();
#if defined(ENABLE_SERVER)
    ws.cleanupClients();
    ElegantOTA.loop();
#endif
#if defined(ENABLE_FIREBASE)
    fb_loop();
#endif
}

bool connectWiFi() {
    unsigned long start = millis();
    Serial.printf("Connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFiClass::status() != WL_CONNECTED) {
        if (millis() - start > 20000) { // timeout after 20 seconds
            Serial.println("Connection failed");
            return false;
        }
        delay(100);
        Serial.print(".");
    }

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

#if defined(ENABLE_SERVER)

void notifyState() {
    if (ws.getClients().empty()) return;
    String message = "R1:" + ValvePower.getStateString() + "\n";
    message += "R2:" + ValveDirection.getStateString() + "\n";
    message += "R3:" + PumpPower.getStateString() + "\n";
    message += "R4:" + ACPower.getStateString() + "\n";
    message += "VALVE:" + Valve.getStateString() + "\n";
    message += "WATER_LEAK:" + WaterLeak.getStateString() + "\n";
    ws.textAll(message);
}

#else
void notifyState() {}
#endif


#if defined(ENABLE_NTP)
void WateringTaskExec(schedule_task_t<WateringTaskArgs> task) {
    if (task.args->waterLiters == 0 && task.args->duration == 0) {
        return;
    }

    // @TODO implement for litters amount
    if (task.args->waterLiters > 0) {
    }

    if (task.args->duration > 0) {
        Valve.open(task.args->duration * 60000L);
    } else {
        // open with default duration
        Valve.open();
    }

    // if specified, open the valve to a certain level
    if (task.args->valveOpenLevel > 0) {
        ValvePower.off();
        delay(1000);
        ValvePower.on(task.args->valveOpenLevel * 10);
    }

    logger.log("VALVE_OPEN", "SCHEDULE", task.args->toString());
}
#endif // ENABLE_NTP