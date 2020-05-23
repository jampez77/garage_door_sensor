#ifndef MY_HELPER_H
#define MY_HELPER_H

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_user = "YOUR_MQTT_USER";
const char* mqtt_password = "YOUR_MQTT_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_SERVER";
const char* stateTopic = "garage/door";
const char* subTopic = "garage/door/output";
const char* doorStatus = "";
const char* prevDoorStatus = "";

const int channel1RelaySwitch = D5;
const int channel2RelaySwitch = D6;


//Statuses
const char* opened = "open";
const char* closed = "closed";
const char* closing = "closing";
const char* opening = "opening";
const char* stopped = "stopped";

//Commands
const char* OPEN = "OPEN";
const char* CLOSE = "CLOSE";
const char* STOP = "STOP";

#endif
