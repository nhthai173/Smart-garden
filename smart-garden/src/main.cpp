#include <Arduino.h>
#include <WiFi.h>

#include <SimpleTimer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include "GenericOutput.h"
#include "GenericInput.h"
#include "VirtualOutput.h"
#include "AutoOff.h"
#include "WateringSchedule.h"
#include "SLog.h"

#include "MAIN_PAGE.h"
#include "secret.h"

#define DEVICE_NAME "Watering System"
#define MDNS_NAME "garden"
#define DEVICE_VERSION "0.2.0"
#define FIRMWARE_VERSION 12

#define FLOW_SENSOR_PIN 35
#define VOLTAGE_PIN 32

#define OUTPUT_ACTIVE_STATE LOW

AutoOff ValvePower(19, OUTPUT_ACTIVE_STATE, 8000L /* 8 sec */); // R1
GenericOutput ValveDirection(18, OUTPUT_ACTIVE_STATE); // R2
AutoOff PumpPower(16, OUTPUT_ACTIVE_STATE); // R3
AutoOff ACPower(4, OUTPUT_ACTIVE_STATE, 180000L /* 3 min */); // R4
VirtualOutput Valve(true, 60000L /* 1 min */);
GenericInput WaterLeak(34, INPUT, LOW);

SimpleTimer timer;

WiFiUDP ntpUDP;
NTPClient timeClient = NTPClient(ntpUDP, "pool.ntp.org", 7 * 3600);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

Scheduler<WateringTaskArgs> scheduler(&timeClient);
SLog logger(&timeClient);


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
void WSHandler(AsyncWebSocket *sv, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len);

void mainLoop() {
    ValvePower.loop();
    ACPower.loop();
    Valve.loop();
    PumpPower.loop();
    timeClient.update();
    scheduler.run();
}


/**
 * List all files in File system
 * @param path
 * @return
 */
String fs_ls(const char *path) {
    String ret = "";
    uint8_t maxLoop = 100; // for safety
    uint8_t loopCnt = 0;
    File file = SPIFFS.open(path);
    if (!file.isDirectory()) return "empty";
    while (true) {
        if (loopCnt >= maxLoop) break;
        file = file.openNextFile();
        if (!file) break;
        ret += file.isDirectory() ? "DIR  " : "FILE ";
        ret += String(file.path()) + " - " + String(file.size()) + " Bytes\n";
        if (file.isDirectory()) {
            ret += fs_ls(file.path());
        }
        file.close();
        loopCnt++;
    }
    return ret;
}

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

/**
 * @brief Execute a watering task from schedule
 *
 * @param task
 */
void WateringTaskExec(schedule_task_t<WateringTaskArgs>);


void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000)
        delay(10);

    if (!connectWiFi()) {
        Serial.println("Failed to connect to WiFi");
    }

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
        logger.log("VALVE_CLOSE", "AUTO_TIMEOUT", "");
    });

    PumpPower.setPowerOnDelay(8000L);
    PumpPower.onPowerChanged(notifyState);

    ValveDirection.onPowerChanged(notifyState);

    WaterLeak.setActiveStateString("LEAK");
    WaterLeak.setInactiveStateString("NONE");
    WaterLeak.onChange([](){
        notifyState();
        logger.log("WATER_LEAK", "SENSOR", WaterLeak.getStateString());
    });
    WaterLeak.onActive([]() {
        Valve.close();
        logger.log("VALVE_CLOSE", "WATER_LEAK", "");
    });

    scheduler.setCallback(WateringTaskExec);

    server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAINPAGE); });

    server.on("/info", HTTP_ANY, [](AsyncWebServerRequest *request) {
        String message = "{";
        message += "\"device\":\"" DEVICE_NAME "\",";
        message += "\"version\":\"" DEVICE_VERSION "\",";
        message += "\"firmware\":" + String(FIRMWARE_VERSION) + ",";
        message += "ip:\"" + WiFi.localIP().toString() + "\",";
        message += "\"valve\":" + Valve.getStateString() + ",";
        message += "\"r1\":" + ValvePower.getStateString() + ",";
        message += "\"r2\":" + ValveDirection.getStateString() + ",";
        message += "\"r3\":" + PumpPower.getStateString() + ",";
        message += "\"r4\":" + ACPower.getStateString() + ",";
        message += "\"r1_auto_off\":" + String(ValvePower.getDuration()) + ",";
        message += "\"r4_auto_off\":" + String(ACPower.getDuration()) + ",";
        message += "\"valve\":" + Valve.getStateString() + ",";
        message += "\"valve_auto_off\":" + String(Valve.getDuration()) + ",";
        message += "\"water_leak\":" + WaterLeak.getStateString();
        message += "}";
        request->send(200, "application/json", message);
    });

//    server.on("/fs", HTTP_GET, [](AsyncWebServerRequest *request) {
//        request->send(200, "text/plain", fs_ls("/"));
//    });

//    Add build_flags -DASYNCWEBSERVER_REGEX to enable uri regex
//    Show file content by go to /file/<path>
//    server.on("^\\/file\\/(.*)$", HTTP_GET, [](AsyncWebServerRequest *request) {
//        String filePath = request->pathArg(0);
//        String ret = "Open file " + filePath + "\n";
//        File f = SPIFFS.open(filePath.c_str(), "r");
//        if (!f || !f.size()) {
//            ret += "File not found or empty";
//            request->send(404, "text/plain", ret);
//            return;
//        }
//        ret += f.readString();
//        request->send(200, "text/plain", ret);
//    });

    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(1000);
        ESP.restart();
    });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Reseting...");
        SPIFFS.format();
        delay(1000);
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

    /* ===================== */

    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);

    ws.onEvent(WSHandler);
    ElegantOTA.begin(&server);
    server.addHandler(&ws);
    server.begin();

    timeClient.begin();

    timer.setInterval(500L, mainLoop);

    logger.clearOldLogs();
    logger.log("START", "SYSTEM", "");
}

void loop() {
    WaterLeak.loop();
    ws.cleanupClients();
    ElegantOTA.loop();
    timer.run();
}

bool connectWiFi() {
    unsigned long start = millis();
    Serial.printf("Connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
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
    if (ws.getClients().isEmpty()) return;
    String message = "R1:" + ValvePower.getStateString() + "\n";
    message += "R2:" + ValveDirection.getStateString() + "\n";
    message += "R3:" + PumpPower.getStateString() + "\n";
    message += "R4:" + ACPower.getStateString() + "\n";
    message += "VALVE:" + Valve.getStateString() + "\n";
    message += "WATER_LEAK:" + WaterLeak.getStateString() + "\n";
    ws.textAll(message);
}

void WSHandler(AsyncWebSocket *sv, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len) {
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
        logger.log("VALVE_OPEN", "SCHEDULE", "");
    }

    // if specified, open the valve to a certain level
    if (task.args->valveOpenLevel > 0) {
        ValvePower.off();
        delay(1000);
        ValvePower.on(task.args->valveOpenLevel * 10);
    }
}