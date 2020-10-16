#ifndef MY_HELPER_H
#define MY_HELPER_H

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_user = "YOUR_MQTT_USER";
const char* mqtt_password = "YOUR_MQTT_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_SERVER";
const char* stateTopic = "homeassistant/cover/garage/door/state";
const char* commandTopic = "homeassistant/cover/garage/door/set";
const char* configTopic = "homeassistant/cover/garage/door/config";
const char* availabilityTopic = "homeassistant/cover/garage/door/availability";
const char* doorStatus = "";
const char* prevDoorStatus = "";

const char* softwareVersion = "1.2";
const char* manufacturer = "NandPez";
const char* model = "Wemos D1 Mini / 1ch Relay / Sommer Conex & Output OC";
String mqttDeviceClientId = "GarageController";
const char* mqttDeviceName = "Garage Door";
const char* mqttDeviceClass = "garage";

int prevDoorState;
int openThreshold = 15;
boolean configDetailsSent = false;

const int relaySwitch = D1;
const int ledPin = D4;
const int doorInput = A0;

const int conexT1 = LOW;
const int conexT2 = HIGH;

const int openDoor = conexT2;
const int closeDoor = conexT1;

//Statuses
const char* opened = "open";
const char* closed = "closed";
const char* closing = "closing";
const char* opening = "opening";
const char* stopped = "stopped";

//pay loads
const char* payloadOpen = "OPEN";
const char* payloadClose = "CLOSE";
const char* payloadStop = "STOP";
const char* payloadAvailable = "online";
const char* payloadNotAvailable = "offline";

#endif
