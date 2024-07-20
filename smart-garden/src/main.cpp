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


// https://github.com/me-no-dev/ESPAsyncWebServer.git
#define ENABLE_SERVER

// mobizt/FirebaseClient@^1.3.5
#define ENABLE_NFIREBASE

#define ENABLE_LOGGER

#define ENABLE_SCHEDULER

#define USE_WEB_INTERFACE

#if defined(ENABLE_SERVER) && defined(USE_WEB_INTERFACE)
#define ENABLE_WEB_INTERFACE
#endif


#include "version.h"

#define DEVICE_NAME "Watering System"

#define FLOW_SENSOR_PIN 35
#define VOLTAGE_PIN 32

#define OUTPUT_ACTIVE_STATE LOW


#include <Arduino.h>
#include <WiFi.h>
#include <SimpleTimer.h>
#include "secret.h" // see secret_placeholder.h

#include "GenericOutput.h"
#include "GenericInput.h"
#include "VirtualOutput.h"
#include "VoltageReader.h"

#if defined(ENABLE_SERVER)

#include <ESPAsyncWebServer.h>

#if defined(ENABLE_WEB_INTERFACE)

#include "MAIN_PAGE.h"

#endif

#endif // ENABLE_SERVER


#if defined(ENABLE_NFIREBASE)

#include "FirebaseIOT.h"
#include "FirebaseRTDBIntegrate.h"

static uint32_t acceptedCallbackMillis = 0;

fbrtdb_object *RTDBObj;

#endif // ENABLE_NFIREBASE

//#include "TimeHelper.h"
//TimeHelper Time;

GenericOutput ValvePower(19, OUTPUT_ACTIVE_STATE, 8000L /* 8 sec */); // R1
GenericOutput ValveDirection(18, OUTPUT_ACTIVE_STATE); // R2
GenericOutput PumpPower(16, OUTPUT_ACTIVE_STATE); // R3
GenericOutput ACPower(4, OUTPUT_ACTIVE_STATE, 180000L /* 3 min */); // R4
VirtualOutput Valve(60000L /* 1 min */);
GenericInput WaterLeak(34, INPUT_PULLUP, LOW);
VoltageReader PowerVoltage(32, 10.0, 3.33, 0.3, 10.5, 13.5);

GenericOutput led(LED_BUILTIN, OUTPUT_ACTIVE_STATE);

SimpleTimer timer;

#if defined(ENABLE_SCHEDULER)

#include "WateringSchedule.h"

void WateringTaskExec(schedule_task_t<WateringTaskArgs> task);

Scheduler<WateringTaskArgs> scheduler(WateringTaskExec);

#endif // ENABLE_SCHEDULER

#if defined(ENABLE_LOGGER)

#include "SLog.h"
//SLog logger(&timeClient, BOT_TOKEN, CHAT_ID);
SLog logger;

#endif // ENABLE_LOGGER


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


String getInfo() {
    String info = "{";
    info += R"("device":")" DEVICE_NAME "\",";
    info += R"("version":")" DEVICE_VERSION "\",";
    info += R"("firmware":)" + String(FIRMWARE_VERSION) + ",";
    info += R"("ip":")" + WiFi.localIP().toString() + "\"";
//    info += R"("timestamp":)" + String(Time.getUnixTime()) + ",";
//    info += R"("datetime":")" + Time.getDateTime() + "\"";

//    info += R"("ip":")" + WiFi.localIP().toString() + "\",";
//    info += R"("voltage":)" + String(PowerVoltage.get()) + ",";
//    info += R"("valve":")" + Valve.getStateString() + "\",";
//    info += R"("r1":")" + ValvePower.getStateString() + "\",";
//    info += R"("r2":")" + ValveDirection.getStateString() + "\",";
//    info += R"("r3":")" + PumpPower.getStateString() + "\",";
//    info += R"("r4":")" + ACPower.getStateString() + "\",";
//    info += R"("r1_auto_off":)" + String(ValvePower.getDuration()) + ",";
//    info += R"("r4_auto_off":)" + String(ACPower.getDuration()) + ",";
//    info += R"("valve":")" + Valve.getStateString() + "\",";
//    info += R"("valve_auto_off":)" + String(Valve.getDuration()) + ",";
//    info += R"("water_leak":")" + WaterLeak.getStateString() + "\"";
    info += "}";
    return info;
}


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
            timer.setTimeout(100L, notifyState);
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
#if defined(ENABLE_LOGGER)
                logger.log("VALVE_" + message.substring(6), "WEB", client->remoteIP().toString());
#endif
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


void responseSuccess(AsyncWebServerRequest *request, const String &message) {
    request->send(200, "text/plain", message);
}

void responseError(AsyncWebServerRequest *request, const String &message) {
    request->send(400, "text/plain", message);
}

#endif


#if defined(ENABLE_NFIREBASE)

void syncRTDB() {
    JsonWriter writer;
    object_t o1, o2, o3, o4, o5, o6, o7, json;
    writer.create(o1, "valve", Valve.getState());
    writer.create(o2, "r1", ValvePower.getState());
    writer.create(o3, "r2", ValveDirection.getState());
    writer.create(o4, "r3", PumpPower.getState());
    writer.create(o5, "r4", ACPower.getState());
    writer.create(o6, "water_leak", WaterLeak.getState());
    writer.create(o7, "restart", false);
    writer.join(json, 7, o1, o2, o3, o4, o5, o6, o7);
    FirebaseIOT.update("/data", json);
}

void updateOTA() {
    Serial.println("Updating firmware...");
    FirebaseIOT.beginOTA("/firmware/bin", [](AsyncResult &res) {
        if (res.isError()) {
            Serial.printf("OTA error: %s, code: %d\n", res.error().message().c_str(), res.error().code());
        } else if (res.downloadProgress()) {
            Firebase.printf("Download task: %s, downloaded %d%s (%d of %d)\n", res.uid().c_str(),
                            res.downloadInfo().progress, "%", res.downloadInfo().downloaded,
                            res.downloadInfo().total);
            if (res.downloadInfo().total == res.downloadInfo().downloaded) {
                Serial.printf("Download completed, task %s\n", res.uid().c_str());
                timer.setTimeout(100L, []() {
                    FirebaseIOT.set("/data/restart", false, [](AsyncResult &res) {
                        Serial.println("Restarting...");
                        ESP.restart();
                    });
                });
            }
        }
    });
}

void checkOTA() {
    FirebaseIOT.get("/firmware/version", [](AsyncResult &res) {
        if (res.available()) {
            auto &RTDB = res.to<RealtimeDatabaseResult>();
            if (RTDB.type() == realtime_database_data_type_integer) {
                if (RTDB.to<int>() <= FIRMWARE_VERSION) {
                    Serial.println("The current firmware is up to date");
                    timer.setTimeout(100L, [](){
                        FirebaseIOT.remove("/firmware");
                        FirebaseIOT.set("/data/restart", false);
                    });
                } else {
                    Serial.printf("New firmware version: %d\n", RTDB.to<int>());
                    timer.setTimeout(100L, updateOTA);
                }
            } else {
                Serial.println("Invalid firmware version");
                timer.setTimeout(100L, [](){
                    FirebaseIOT.set("/data/restart", false);
                });
            }
        }
    });
}

#endif


void mainLoop() {
    ValvePower.loop();
    ACPower.loop();
    Valve.loop();
    PumpPower.loop();
    PowerVoltage.loop();
#if defined(ENABLE_LOGGER)
    logger.loop();
#endif
}


#if defined(ENABLE_SCHEDULER)

/**
 * @brief Execute a watering task from schedule
 *
 * @param task
 */
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
#if defined(ENABLE_LOGGER)
    logger.log("VALVE_OPEN", "SCHEDULE", task.args->toString());
#endif
}

#endif // ENABLE_SCHEDULER


void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000)
        delay(10);

    if (!connectWiFi()) {
        Serial.println("Failed to connect to WiFi");
    }

    configTime(7 * 3600, 0, "pool.ntp.org");

//    logger.setTeleLogPrefix("🌱 Vườn cây");

    timer.setInterval(1000L, []() {
        led.toggle();
        Serial.print(".");
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
#if defined(ENABLE_LOGGER)
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
#if defined(ENABLE_LOGGER)
        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
        logger.log("VALVE_CLOSE", "WATER_LEAK", "");
        logger.logTele("🚨 Phát hiện ngập nước");
#endif
    });
    WaterLeak.onHoldState(false, 5000L, []() {
#if defined(ENABLE_LOGGER)
        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
        logger.logTele("🚰 Nước đã hết ngập");
#endif
    });

#if defined(ENABLE_SERVER)
    server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });

#if defined(ENABLE_WEB_INTERFACE)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAINPAGE); });
#endif

    server.on("/info", HTTP_ANY, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getInfo());
    });


    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        responseSuccess(request, "Restarting...");
        delay(1000);
        ESP.restart();
    });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        responseSuccess(request, "Reseting...");
#if defined(SPIFFS_H) && (defined(ENABLE_LOGGER) || defined(ENABLE_SCHEDULER))
        SPIFFS.format();
        SPIFFS.end();
#endif
        ESP.restart();
    });


    /**
     * Endpoint /watering
     * @param stop - stop watering
     */
    server.on("/watering", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (request->hasParam("stop")) {
            Valve.close();
#if defined(ENABLE_LOGGER)
            logger.log("VALVE_CLOSE", "API", request->client()->remoteIP().toString());
#endif
        } else {
            Valve.open();
#if defined(ENABLE_LOGGER)
            logger.log("VALVE_OPEN", "API", request->client()->remoteIP().toString());
#endif
        }
        responseSuccess(request, "OK");
    });

#if defined(ENABLE_LOGGER)
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        responseSuccess(request, logger.getLogs());
    });
#endif

#if defined(ENABLE_SCHEDULER)
    server.on("/schedules", HTTP_GET, [](AsyncWebServerRequest *request) {
#if defined(ENABLE_NFIREBASE)
        request->send(200, "application/json", scheduler.toArray());
#else
        String ret = "Object task count: " + String(scheduler.getTaskCount()) + "\n";
        ret += "Read from file:\n" + scheduler.getString();
        request->send(200, "text/plain", ret);
#endif
    });

    server.on("/add-schedule", [](AsyncWebServerRequest *request) {
        if (!request->hasParam("time")) {
            return responseError(request, "missing time");
        }
        bool isRepeatSet = false;
        schedule_task_t<WateringTaskArgs> task = {
                .enabled = true,
                .executed = false,
        };
        task.id = scheduler.generateUid();
        task.time = scheduler.parseTime(request->getParam("time")->value());
        task.repeat = {};
        task.args = new WateringTaskArgs();
        if (request->hasParam("r1")) {
            task.repeat.sunday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r2")) {
            task.repeat.monday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r3")) {
            task.repeat.tuesday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r4")) {
            task.repeat.wednesday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r5")) {
            task.repeat.thursday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r6")) {
            task.repeat.friday = true;
            isRepeatSet = true;
        }
        if (request->hasParam("r7")) {
            task.repeat.saturday = true;
            isRepeatSet = true;
        }
        if (!isRepeatSet) {
            task.repeat = {
                    .monday = true,
                    .tuesday = true,
                    .wednesday = true,
                    .thursday = true,
                    .friday = true,
                    .saturday = true,
                    .sunday = true,
            };
        }
        if (request->hasParam("duration")) {
            task.args->duration = request->getParam("duration")->value().toInt();
        }
//        if (request->hasParam("water_liters")) {
//            task.args->waterLiters = request->getParam("water_liters")->value().toInt();
//        }
        if (request->hasParam("valve_level")) {
            task.args->valveOpenLevel = request->getParam("valve_level")->value().toInt();
        }
        if (scheduler.addTask(task)) {
            responseSuccess(request, "Schedule added");
        } else {
            delete task.args;
            responseError(request, "Failed to add schedule");
        }
    });

    server.on("/remove-schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("id")) {
            return responseError(request, "missing id");
        }
        uint8_t id = request->getParam("id")->value().toInt();
        if (!id) {
            return responseError(request, "Invalid id");
        }
        responseSuccess(request, scheduler.removeTask(id) ? "Schedule removed" : "Schedule not found");
    });
#endif

    /* ===================== */

    ws.onEvent(WSHandler);
    server.addHandler(&ws);
    server.begin();
#endif

#if defined(ENABLE_LOGGER)
    logger.clearOldLogs();
    logger.log("START", "SYSTEM", String(FIRMWARE_VERSION));
#endif // ENABLE_LOGGER

    timer.setInterval(500L, mainLoop);

#if defined(ENABLE_SCHEDULER)
    timer.setInterval(3000L, []() {
        scheduler.run();
    });
#endif


#if defined(ENABLE_NFIREBASE)
    RTDBObj = new fbrtdb_object();
    RTDBObj->client = &aClient;
    RTDBObj->rtdb = &Database;

    // The path should be set after user is authenticated
    // <user_id>/<device_id>/data
    RTDBObj->prefixPath = FirebaseIOT.DB_DEVICE_PATH + "/data";

    Valve.attachDatabase(RTDBObj, "/valve", stdGenericOutput::START_UP_OFF, printResult);
    ValvePower.attachDatabase(RTDBObj, "/r1", stdGenericOutput::START_UP_OFF, printResult);
    ValveDirection.attachDatabase(RTDBObj, "/r2", stdGenericOutput::START_UP_OFF, printResult);
    PumpPower.attachDatabase(RTDBObj, "/r3", stdGenericOutput::START_UP_OFF, printResult);
    ACPower.attachDatabase(RTDBObj, "/r4", stdGenericOutput::START_UP_OFF, printResult);

#if defined(ENABLE_SCHEDULER)
    scheduler.attachDatabase(RTDBObj);
#endif

    timer.setInterval(250, []() {
        FirebaseIOT.loop();
    });

    FirebaseIOT.begin(API_KEY, DATABASE_URL, USER_EMAIL, USER_PASSWORD);
    FirebaseIOT.onFirstConnected([]() {
        // Update path
        RTDBObj->prefixPath = FirebaseIOT.DB_DEVICE_PATH + "/data";

#ifdef ENABLE_SCHEDULER
        scheduler.setPath(FirebaseIOT.DB_DEVICE_PATH + "/schedules");
#endif

        // Set info
        FirebaseIOT.update("/info", (object_t) getInfo());

        // Load schedules
#ifdef ENABLE_SCHEDULER
        scheduler.load();
#endif


        // Set stream
        FirebaseIOT.setStream("/data", [](AsyncResult &res) {
            printResult(res);

            auto &RTDB = res.to<RealtimeDatabaseResult>();
            // First time connect -> intit data
            if (RTDB.event() == "put" && RTDB.dataPath() == "/" && RTDB.type() == realtime_database_data_type_null) {
                syncRTDB();
            } else if (RTDB.dataPath() == "/" && RTDB.type() == realtime_database_data_type_json) {
                JSON::JsonParser parser(RTDB.to<String>());
                Valve.setState(parser.get<bool>("valve"));
                ValvePower.setState(parser.get<bool>("r1"));
                ValveDirection.setState(parser.get<bool>("r2"));
                PumpPower.setState(parser.get<bool>("r3"));
                ACPower.setState(parser.get<bool>("r4"));
                notifyState();

                if (parser.get<bool>("water_leak") != WaterLeak.getState()) {
                    FirebaseIOT.set("/data/water_leak", WaterLeak.getState());
                }
            } else if (RTDB.dataPath() == "/restart") {
                String v = RTDB.to<String>();
                if (v == "ota") {
                    Serial.printf("Checking OTA...");
                    checkOTA();
                } else if (v == "true") {
                    FirebaseIOT.set("/data/restart", false, [](AsyncResult &res) {
                        Serial.println("Restarting...");
                        ESP.restart();
                    });
                }
            } else if (RTDB.dataPath() == "/water_leak" && RTDB.to<bool>() != WaterLeak.getState()) {
                FirebaseIOT.set("/data/water_leak", WaterLeak.getState());
            } else {
                if (RTDB.dataPath() == "/r3") {
                    PumpPower.setState(RTDB.to<bool>());
                } else if (RTDB.dataPath() == "/valve") {
                    Valve.setState(RTDB.to<bool>());
                } else if (millis() > acceptedCallbackMillis) {
                    if (RTDB.dataPath() == "/r1") {
                        ValvePower.setState(RTDB.to<bool>());
                    } else if (RTDB.dataPath() == "/r2") {
                        ValveDirection.setState(RTDB.to<bool>());
                    } else if (RTDB.dataPath() == "/r4") {
                        ACPower.setState(RTDB.to<bool>());
                    }
                }
            }

            notifyState();
        });

        // Erase old Firmware and update new
        checkOTA();

    }); // onFirstConnected

#endif // ENABLE_NFIREBASE


} // setup





void loop() {
    WaterLeak.loop();
    timer.run();
#if defined(ENABLE_SERVER)
    ws.cleanupClients();
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


void notifyState() {

#if defined(ENABLE_NFIREBASE)
    acceptedCallbackMillis = millis() + 10000;
#endif

#if defined(ENABLE_SERVER)
    if (ws.getClients().isEmpty()) return;
    String message = "R1:" + ValvePower.getStateString() + "\n";
    message += "R2:" + ValveDirection.getStateString() + "\n";
    message += "R3:" + PumpPower.getStateString() + "\n";
    message += "R4:" + ACPower.getStateString() + "\n";
    message += "VALVE:" + Valve.getStateString() + "\n";
    message += "WATER_LEAK:" + WaterLeak.getStateString() + "\n";
    ws.textAll(message);
#endif

}