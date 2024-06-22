#include <Arduino.h>
#include <WiFi.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include "MAIN_PAGE.h"
#include "secret.h"

#define VALVE_POWER_PIN 19     // R1
#define VALVE_DIRECTION_PIN 18 // R2
#define PUMP_POWER_PIN 16      // R3
#define AC_POWER_PIN 4         // R4
#define WATER_LEAK_PIN 34
#define FLOW_SENSOR_PIN 35
#define VOLTAGE_PIN 32

#define OUTPUT_ACTIVE_STATE LOW

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool connectWiFi();
String stringState(bool state);
bool boolState(String state);
void notifyState(AsyncWebSocket *server);
void WSHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void setup()
{
  pinMode(VALVE_DIRECTION_PIN, OUTPUT);
  pinMode(VALVE_POWER_PIN, OUTPUT);
  pinMode(PUMP_POWER_PIN, OUTPUT);
  pinMode(AC_POWER_PIN, OUTPUT);

  digitalWrite(VALVE_DIRECTION_PIN, !OUTPUT_ACTIVE_STATE);
  digitalWrite(VALVE_POWER_PIN, !OUTPUT_ACTIVE_STATE);
  digitalWrite(PUMP_POWER_PIN, !OUTPUT_ACTIVE_STATE);
  digitalWrite(AC_POWER_PIN, !OUTPUT_ACTIVE_STATE);

  Serial.begin(115200);
  while (!Serial)
    delay(10);

  if (!connectWiFi())
  {
    Serial.println("Failed to connect to WiFi");
    while (1)
      delay(100);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", MAINPAGE); });

  ws.onEvent(WSHandler);

  ElegantOTA.begin(&server);
  server.addHandler(&ws);
  server.begin();
}

void loop()
{
  ws.cleanupClients();
  ElegantOTA.loop();
}

bool connectWiFi()
{
  unsigned long start = millis();
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - start > 20000)
    { // timeout after 20 seconds
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

String stringState(bool state)
{
  return state == OUTPUT_ACTIVE_STATE ? "ON" : "OFF";
}

bool boolState(String state)
{
  return state.startsWith("ON") ? OUTPUT_ACTIVE_STATE : !OUTPUT_ACTIVE_STATE;
}

void notifyState(AsyncWebSocket *server)
{
  String message = "R1:" + stringState(digitalRead(VALVE_POWER_PIN)) + "\n";
  message += "R2:" + stringState(digitalRead(VALVE_DIRECTION_PIN)) + "\n";
  message += "R3:" + stringState(digitalRead(PUMP_POWER_PIN)) + "\n";
  message += "R4:" + stringState(digitalRead(AC_POWER_PIN));
  server->textAll(message);
}

void WSHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  String message = (char *)data;
  bool boolControlVal = boolState(message.substring(3));

  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WS client [%d] connected\n", client->id());
    notifyState(server);
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WS client [%d] disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    Serial.printf("WS client [%d] message: %s\n", client->id(), message.c_str());
    if (message.startsWith("R1:"))
    {
      digitalWrite(VALVE_POWER_PIN, boolControlVal);
    }
    else if (message.startsWith("R2:"))
    {
      digitalWrite(VALVE_DIRECTION_PIN, boolControlVal);
    }
    else if (message.startsWith("R3:"))
    {
      digitalWrite(PUMP_POWER_PIN, boolControlVal);
    }
    else if (message.startsWith("R4:"))
    {
      digitalWrite(AC_POWER_PIN, boolControlVal);
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