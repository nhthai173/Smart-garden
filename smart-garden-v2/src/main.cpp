// https://github.com/me-no-dev/ESPAsyncWebServer.git
#define ENABLE_SERVER

// mobizt/FirebaseClient@^1.3.5
//#define ENABLE_NFIREBASE

// #define ENABLE_LOGGER

//#define ENABLE_SCHEDULER

#define USE_WEB_INTERFACE

#if defined(ENABLE_SERVER) && defined(USE_WEB_INTERFACE)
#define ENABLE_WEB_INTERFACE
#endif

#define MAIN_DEBUG 1

#if MAIN_DEBUG == 1
#define DPRINTF(...) { Serial.print("[MAIN] "); Serial.printf(__VA_ARGS__); }
#else
#define DPRINTF(...)
#endif


#include "version.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include "secret.h" // see secret_placeholder.h

#include "GenericOutput.h"
#include "GenericInput.h"
#include "VirtualOutput.h"
#include "NTimer.h"
//#include "WateringSchedule.h"

// Web server
#include <ESPAsyncWebServer.h>
#include "MAIN_PAGE.h"


// === Devices ===
GenericOutput ValvePower(PIN_VALVE_POWER, OUTPUT_ACTIVE_STATE, START_UP_OFF, TIME_PUMP_POWER_AUTO_OFF);
GenericOutput ValveDirection(PIN_VALVE_DIRECTION, OUTPUT_ACTIVE_STATE, START_UP_OFF);
GenericOutput PumpPower(PIN_PUMP_POWER, OUTPUT_ACTIVE_STATE, START_UP_OFF);
GenericOutput ACPower(PIN_AC_POWER, OUTPUT_ACTIVE_STATE, START_UP_OFF, PIN_AC_POWER);
VirtualOutput Valve;
GenericInput WaterLeak(34, INPUT_PULLUP, LOW);
//VoltageReader PowerVoltage(32, 10.0, 2, 0.3, 10.5, 13.5);
bool FLAG_PUMP_POWER_STABLE = false;

// === Helpers ===
NTimer timer;

//void WateringTaskExec(schedule_task_t<WateringTaskArgs> task);
//Scheduler<WateringTaskArgs> scheduler(WateringTaskExec);

// === WEB SERVER ===
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");



/* ========================== Functions forward declaration ========================== */

/**
 * @brief Notify all clients of the current state
 *
 * @param server
 */
void notifyState();

String getInfo();

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
void WSHandler(AsyncWebSocket *sv, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void responseSuccess(AsyncWebServerRequest *request, const String &message) {
    request->send(200, "text/plain", message);
}

void responseError(AsyncWebServerRequest *request, const String &message) {
    request->send(400, "text/plain", message);
}

/* ========================== END Functions forward declaration ========================== */



/* ========================== SETUP ========================== */

void setupWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    DPRINTF("Connecting to WiFi\n");
    WiFi.onEvent([](arduino_event_id_t ev) {
        DPRINTF("Connected to WiFi, IP: %s\n", WiFi.localIP().toString().c_str());
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

void setupDevices() {
    ValveDirection.onPowerChanged([](){
//        notifyState();
        bool v = ValveDirection.getState();
        DPRINTF("R1 - %d", v);
    });

    ValvePower.onPowerOff([]() {
        ValveDirection.off();
    });
    ValvePower.onPowerChanged([](){
//        notifyState();
        bool v = ValvePower.getState();
        DPRINTF("R2 - %d", v);
    });

    PumpPower.setPowerOnDelay(TIME_PUMP_POWER_ON_DELAY);
    PumpPower.onPowerChanged([](){
//        notifyState();
        bool v = PumpPower.getState();
        DPRINTF("R3 - %d", v);
    });

    ACPower.onPowerOn([]() {
        timer.setTimeout(1000L, []() {
            FLAG_PUMP_POWER_STABLE = true;
        });
    });
    ACPower.onPowerOff([]() {
        ValvePower.off();
        PumpPower.off();
        FLAG_PUMP_POWER_STABLE = false;
    });
    ACPower.onPowerChanged([](){
//        notifyState();
        bool v = ACPower.getState();
        DPRINTF("R4 - %d", v);
    });


    Valve.setOnFunction([]() {
        if (WaterLeak.getState()) {
            Valve.off(true);
#if defined(ENABLE_LOGGER)
            logger.logTele("🚨 Không thể mở van vì phát hiện ngập nước");
#endif
        } else {
            ValveDirection.off();
            ACPower.on(true);
            if (FLAG_PUMP_POWER_STABLE) {
                ValvePower.on();
                PumpPower.on();
            } else {
                timer.setTimeout(1000L, []() {
                    ValvePower.on();
                    PumpPower.on();
                });
            }
        }
    });
    Valve.setOffFunction([]() {
        PumpPower.off();
        ValveDirection.on();
        ACPower.on();
        if (FLAG_PUMP_POWER_STABLE) {
            ValvePower.on();
        } else {
            timer.setTimeout(1000L, []() {
                ValvePower.on();
            });
        }
    });
    Valve.onPowerChanged(notifyState);
    Valve.onPowerOn([]() {
        if (!Valve.getState()) return; // ignore if not open

#if defined(ENABLE_LOGGER)
        if (update_valve_state.timer > -1) {
            timer.deleteTimer(update_valve_state.timer);
            update_valve_state.msgId = 0;
        }
        String mes = "*🌱 Đã bắt đầu tưới nước*\nThời gian còn lại: `" + Valve.getRemainingTimeString() + "`";
        logger.logTele(mes, [](uint16_t msgId) {
            update_valve_state.msgId = msgId;
        });
        update_valve_state.timer = timer.setInterval(10000L, []() {
            if (!Valve.getState()) {
                logger.logTeleEdit(update_valve_state.msgId, "🌱 Đã kết thúc tưới nước");
                timer.deleteTimer(update_valve_state.timer);
                update_valve_state.timer = -1;
                update_valve_state.msgId = 0;
            } else {
                logger.logTeleEdit(
                        update_valve_state.msgId,
                        "*🌱 Đã bắt đầu tưới nước*\nThời gian còn lại: `" + Valve.getRemainingTimeString() + "`",
                        [](uint16_t msgId) {
                            if (!update_valve_state.msgId)
                                update_valve_state.msgId = msgId;
                        });
            }
        });
#endif
    });

    Valve.onAutoOff([]() {
#if defined(ENABLE_LOGGER)
        logger.log("VALVE_CLOSE", "AUTO_TIMEOUT", "");
        logger.logTele("🌱 Đã tự động tắt nước");
#endif // ENABLE_LOGGER
    });


//    WaterLeak.setActiveStateString("LEAK");
//    WaterLeak.setInactiveStateString("NONE");
    WaterLeak.onChange(notifyState);
//    WaterLeak.onHoldState(true, 5000L, []() {
//        Valve.close(true);
//#if defined(ENABLE_LOGGER)
//        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
//        logger.log("VALVE_CLOSE", "WATER_LEAK", "");
//        logger.logTele("🚨 Phát hiện ngập nước");
//#endif // ENABLE_LOGGER
//    });
//    WaterLeak.onHoldState(false, 5000L, []() {
//#if defined(ENABLE_LOGGER)
//        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
//        logger.logTele("🚰 Nước đã hết ngập");
//#endif // ENABLE_LOGGER
//    });
//#if defined(ENABLE_NFIREBASE)
//    WaterLeak.setDatabaseReportStateDelay(5000L);
//#endif

    ValveDirection.begin();
    PumpPower.begin();
    ValvePower.begin();
    ACPower.begin();
}

/* ========================== END SETUP ========================== */





void mainLoop() {
#if defined(ENABLE_LOGGER)
    logger.loop();
#endif
#if defined(ENABLE_SERVER)
    ws.cleanupClients();
#endif
}




void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000)
        delay(10);

    configTime(7 * 3600, 0, "pool.ntp.org");

    setupDevices();

    setupWifi();


//    logger.setTeleLogPrefix("🌱 Vườn cây");

    timer.setInterval(1000L, []() {
        Serial.print(".");
        // Serial.printf("Free heap: %d\n", esp_get_free_heap_size());
    });


    server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAINPAGE); });

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
        delay(1000);
#if defined(_SPIFFS_H_) && (defined(ENABLE_LOGGER) || defined(ENABLE_SCHEDULER))
        SPIFFS.format();
        SPIFFS.end();
        delay(2000);
#endif // SPIFFS_H
        ESP.restart();
    });

    server.on("/set", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("ch") || !request->hasParam("v")) {
            return responseError(request, "missing \"ch\" or \"state\" parameter");
        }

        String ch = request->getParam("ch")->value();
        ch.toLowerCase();
        String v = request->getParam("v")->value();
        v.toLowerCase();
        bool value;
        bool executed = false;

        if (v == "1" || v == "on" || v == "true" || v == "open" || v == "active") {
            value = true;
        } else if (v == "0" || v == "off" || v == "false" || v == "close" || v == "inactive") {
            value = false;
        } else {
            return responseError(request, "Invalid state value");
        }

        if (ch == "r1") {
            ValvePower.setState(value);
        } else if (ch == "r2") {
            ValveDirection.setState(value);
        } else if (ch == "r3") {
            PumpPower.setState(value);
        } else if (ch == "r4") {
            ACPower.setState(value);
        } else if (ch == "valve") {
            Valve.setState(value);
        } else {
            return responseError(request, "Invalid channel");
        }

        responseSuccess(request, "OK");
    });

    server.on("/watering", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (request->hasParam("stop")) {
            Valve.off(true);
#if defined(ENABLE_LOGGER)
            logger.log("VALVE_CLOSE", "API", request->client()->remoteIP().toString());
#endif // ENABLE_LOGGER
        } else {
            Valve.on(true);
#if defined(ENABLE_LOGGER)
            logger.log("VALVE_OPEN", "API", request->client()->remoteIP().toString());
#endif // ENABLE_LOGGER
        }
        responseSuccess(request, "OK");
    });

#if defined(ENABLE_LOGGER)
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        responseSuccess(request, logger.getLogs());
    });
#endif // ENABLE_LOGGER

#if defined(ENABLE_SCHEDULER)
    server.on("/schedules", HTTP_GET, [](AsyncWebServerRequest *request) {
#if defined(ENABLE_NFIREBASE)
        request->send(200, "application/json", scheduler.toArray());
#else
        String ret = "Object task count: " + String(scheduler.getTaskCount()) + "\n";
        ret += "Read from file:\n" + scheduler.getString();
        request->send(200, "text/plain", ret);
#endif // ENABLE_NFIREBASE
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
#if defined(ENABLE_LOGGER)
            String log = "*🌱 Đã thêm 1 hẹn giờ mới*\n";
            log += "ID: `" + String(task.id) + "`\n";
            log += "Thời gian: " + task.time.toString() + "\n";
            log += "Thời lượng: " + String(task.args->duration) + " phút\n";
            log += "Mức mở van: " + String(task.args->valveOpenLevel * 10) + "%\n";
            log += "Lặp lại: " + task.repeat.toString() + "\n";
            logger.logTele(log);
#endif // ENABLE_LOGGER
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
#if defined(ENABLE_LOGGER)
        logger.logTele("🌱 Đã xóa 1 hẹn giờ");
#endif // ENABLE_LOGGER
    });
#endif // ENABLE_SCHEDULER

    server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest *request) {
//        responseSuccess(request, String(PowerVoltage.get()));
        responseSuccess(request, "Not implemented");
    });

    /* ===================== */

    ws.onEvent(WSHandler);
    server.addHandler(&ws);
    server.begin();


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

} // setup





void loop() {
    timer.loop();
    GPIO_Scheduler.run();
}







/**
 * @brief Execute a watering task from schedule
 *
 * @param task
 */
//void WateringTaskExec(schedule_task_t<WateringTaskArgs> task) {
//    if (task.args->waterLiters == 0 && task.args->duration == 0) {
//        return;
//    }
//
//    // @TODO implement for litters amount
//    if (task.args->waterLiters > 0) {
//    }
//
//    if (task.args->duration > 0) {
//        Valve.on(0, task.args->duration * 60000L, true);
//    } else {
//        // open with default duration
//        Valve.on(true);
//    }
//
//    // if specified, open the valve to a certain level
//    if (task.args->valveOpenLevel > 0) {
//        ValvePower.onPercentage(task.args->valveOpenLevel * 10);
//    }
//#if defined(ENABLE_LOGGER)
//    logger.log("VALVE_OPEN", "SCHEDULE", task.args->toString());
//#endif
//}


void notifyState() {

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
            if (message.startsWith("GET")) {
                notifyState();
            } else if (message.startsWith("R1:")) {
                ValvePower.setState(message.substring(3));
            } else if (message.startsWith("R2:")) {
                ValveDirection.setState(message.substring(3));
            } else if (message.startsWith("R3:")) {
                PumpPower.setState(message.substring(3));
            } else if (message.startsWith("R4:")) {
                ACPower.setState(message.substring(3));
            } else if (message.startsWith("VALVE:")) {
                String cmd = message.substring(6);
                if (cmd.startsWith("OPEN")) {
                    cmd = "OPEN";
                    Valve.on(true);
                } else if (cmd.startsWith("CLOSE")) {
                    cmd = "CLOSE";
                    Valve.off(true);
                } else {
                    cmd = "";
                }
#if defined(ENABLE_LOGGER)
                if (cmd != "") {
                    logger.log("VALVE_" + cmd, "WEB", client->remoteIP().toString());
                }
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

