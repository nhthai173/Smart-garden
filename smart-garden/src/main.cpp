#include <Arduino.h>
#include <WiFi.h>

#include <SimpleTimer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>

#include "IODevice.h"
#include "AutoOff.h"
#include "VirtualDevice.h"

#include "MAIN_PAGE.h"
#include "secret.h"

#define DEVICE_NAME "Watering System"
#define MDNS_NAME "garden"
#define DEVICE_VERSION "0.0.2"
#define FIRMWARE_VERSION 2

//#define VALVE_POWER_PIN 19     // R1
//#define VALVE_DIRECTION_PIN 18 // R2
//#define PUMP_POWER_PIN 16      // R3
//#define AC_POWER_PIN 4         // R4
#define WATER_LEAK_PIN 34
#define FLOW_SENSOR_PIN 35
#define VOLTAGE_PIN 32

#define OUTPUT_ACTIVE_STATE LOW

AutoOff ValvePower(19, 8000   /* 8 sec */, OUTPUT_ACTIVE_STATE); // R1
IODevice ValveDirection(18, OUTPUT_ACTIVE_STATE); // R2
IODevice PumpPower(16, OUTPUT_ACTIVE_STATE); // R3
AutoOff ACPower(4, 300000 /* 5 min */, OUTPUT_ACTIVE_STATE); // R4
VirtualDevice Valve;

SimpleTimer timer;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


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
void notifyState(AsyncWebSocket *server);

/**
 * @brief WebSocket event handler
 * 
 * @param server 
 * @param client 
 * @param type 
 * @param arg 
 * @param data 
 * @param len 
 */
void WSHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len);


void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000)
        delay(10);

    if (!connectWiFi()) {
        Serial.println("Failed to connect to WiFi");
    }

    Valve.setOnStateString("OPEN");
    Valve.setOffStateString("CLOSE");

    ACPower.onPowerOn([]() {
        delay(1000); // wait for power to stabilize
    });
    ACPower.onPowerOff([]() {
        ValvePower.off();
        PumpPower.off();
    });
    ValvePower.onPowerOff([]() {
        ValveDirection.off();
    });

    Valve.setOnFunction([]() {
        ValveDirection.off();
        ACPower.on();
        ValvePower.on();
    });

    Valve.setOffFunction([]() {
        ValveDirection.on();
        ACPower.on();
        ValvePower.on();
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAINPAGE); });

    server.on("/info", HTTP_ANY, [](AsyncWebServerRequest *request) {
        String message = "{";
        message += "\"device\":\"" DEVICE_NAME "\",";
        message += "\"version\":\"" DEVICE_VERSION "\",";
        message += "\"firmware\":" + String(FIRMWARE_VERSION) + ",";
        message += "\"valve\":" + Valve.getStateString() + ",";
        message += "\"r1\":" + ValvePower.getStateString() + ",";
        message += "\"r2\":" + ValveDirection.getStateString() + ",";
        message += "\"r3\":" + PumpPower.getStateString() + ",";
        message += "\"r4\":" + ACPower.getStateString() + ",";
        message += "\"valve\":" + Valve.getStateString();
        message += "}";
        request->send(200, "application/json", message);
    });

    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);

    ws.onEvent(WSHandler);
    ElegantOTA.begin(&server);
    server.addHandler(&ws);
    server.begin();
}

void loop() {
    ws.cleanupClients();
    ElegantOTA.loop();
    timer.run();
    ValvePower.loop();
    ACPower.loop();
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

void notifyState(AsyncWebSocket *server) {
    String message = "R1:" + ValvePower.getStateString() + "\n";
    message += "R2:" + ValveDirection.getStateString() + "\n";
    message += "R3:" + PumpPower.getStateString() + "\n";
    message += "R4:" + ACPower.getStateString() + "\n";
    message += "VALVE:" + Valve.getStateString();
    server->textAll(message);
}

void WSHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len) {
    String message = (char *) data;

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client [%d] connected\n", client->id());
            notifyState(server);
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
            notifyState(server);
            break;
        case WS_EVT_PONG:
            break;
        case WS_EVT_ERROR:
            break;
        default:
            break;
    }
}