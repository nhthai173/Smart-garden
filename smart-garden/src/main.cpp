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

#include "MAIN_PAGE.h"
#include "secret.h"

#define DEVICE_NAME "Watering System"
#define MDNS_NAME "garden"
#define DEVICE_VERSION "0.0.10"
#define FIRMWARE_VERSION 10

#define WATER_LEAK_PIN 34
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

bool scheduleLoopExecuted = false;


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

    // @TODO add Scheduler class to handle this
    if (timeClient.getHours() == 7 && timeClient.getMinutes() == 0) {
        if (!scheduleLoopExecuted) {
            scheduleLoopExecuted = true;
            Valve.open();
        }
    } else if (scheduleLoopExecuted) {
        scheduleLoopExecuted = false;
    }
}


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

    PumpPower.setPowerOnDelay(8000L);
    PumpPower.onPowerChanged(notifyState);

    ValveDirection.onPowerChanged(notifyState);

    WaterLeak.setActiveStateString("LEAK");
    WaterLeak.setInactiveStateString("NONE");
    WaterLeak.onChange(notifyState);
    WaterLeak.onActive([]() {
        Valve.close();
    });

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

    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(1000);
        ESP.restart();
    });

    server.on("/watering", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (request->hasParam("stop")) {
            Valve.close();
        } else {
            Valve.open();
        }
        request->send(200, "text/plain", "OK");
    });

    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);

    ws.onEvent(WSHandler);
    ElegantOTA.begin(&server);
    server.addHandler(&ws);
    server.begin();

    timeClient.begin();

    timer.setInterval(500L, mainLoop);
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