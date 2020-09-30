
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "My_Helper.h"

//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//
//ALL CONFIG CHANGES ARE LOCATED IN My_Helper.h//

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  //Set WiFi mode so we don't create an access point.
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.println(WiFi.mode(WIFI_STA));
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  randomSeed(micros());
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

boolean connectClient() {
  // Loop until we're connected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Check connection
    if (client.connect(mqttDeviceClientId.c_str(), mqtt_user, mqtt_password, availabilityTopic, 0, true, payloadNotAvailable)) {
      // Make an announcement when connected
      Serial.println("connected");
      client.publish(availabilityTopic, payloadAvailable, true);  
      
      client.subscribe(commandTopic);
      client.subscribe(availabilityTopic);
      
      Serial.println("Subscribed to: ");
      Serial.println(commandTopic);
      Serial.println(availabilityTopic);
      return true;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      return false;
    }
  }
  return true;
}


void setup() {

  pinMode(relaySwitch, OUTPUT);

  pinMode(doorInput, INPUT);
  
  Serial.begin(115200);
 
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  if (connectClient()) {
    //Send cover entity details to home assistant on initial connection
    //for auto discovery
    StaticJsonDocument<1024> mqttConfig;
    mqttConfig["name"] = mqttDeviceName;
    mqttConfig["dev_cla"] = mqttDeviceClass; 
    mqttConfig["stat_t"] = stateTopic;
    mqttConfig["cmd_t"] = commandTopic; 
    mqttConfig["stat_open"] = opened;
    mqttConfig["stat_clsd"] = closed;
    mqttConfig["stat_closing"] = closing;
    mqttConfig["stat_opening"] = opening;
    mqttConfig["pl_open"] = payloadOpen;
    mqttConfig["pl_cls"] = payloadClose;
    mqttConfig["pl_stop"] = payloadStop;
    mqttConfig["opt"] = false;
    mqttConfig["ret"] = true;
    mqttConfig["avty_t"] = availabilityTopic;
    mqttConfig["pl_avail"] = payloadAvailable;
    mqttConfig["pl_not_avail"] = payloadNotAvailable;
    mqttConfig["uniq_id"] = mqttDeviceClientId;
    
    mqttConfig["dev"]["name"] = mqttDeviceClientId;
    mqttConfig["dev"]["mf"] = manufacturer;
    mqttConfig["dev"]["mdl"] = model;
    mqttConfig["dev"]["sw"] = softwareVersion;
    mqttConfig["dev"]["ids"][0] = mqttDeviceClientId;
    
    char json[1024];
    serializeJsonPretty(mqttConfig, json);
    client.publish(configTopic, json, true); 

    //send initial door status without triggering relay
    getAndSendDoorStatus();
  }

  //Setup OTA
  {

    //Authentication to avoid unauthorized updates
    //ArduinoOTA.setPassword((const char *)"nidayand");

    ArduinoOTA.setHostname((mqttDeviceClientId + "-" + String(ESP.getChipId())).c_str());

    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
  }
}

void loop() {

  //OTA client code
  ArduinoOTA.handle();

  //while connected we send the current door status
  //and trigger relay if we need to
  if(client.connected()){
    client.loop();
  
    delay(1000);
    getAndSendDoorStatus();
  } else {
    connectClient();
  }
}

void callback(char* topic, byte* message, unsigned int length) {

  String messageStr;
  
  for (int i = 0; i < length; i++) {
    messageStr += (char)message[i];
  }
  
  if (String(topic) == commandTopic) {
     Serial.print("Home Assistant Command: ");
     Serial.println(messageStr);

     if((messageStr == payloadOpen && doorStatus != opened) || messageStr == "forceOpen"){
        //open door is not already open or we are running a test.

        //trigger relay to open

        if(doorStatus == closed){
           closeTheDoor(); 
           delay(500);
        } 
         //change to transition state opening
        client.publish(stateTopic, opening, true); 
        openTheDoor();
     }else if((messageStr == payloadClose && doorStatus != closed) || messageStr == "forceClose"){
        //close door is not already closed or we are running a test

        //trigger relay to close
       
        if(doorStatus == opened){
           openTheDoor(); 
           delay(500);
        } 
        //change to transition state closing
        client.publish(stateTopic, closing, true); 
        closeTheDoor(); 
     }else if(messageStr == payloadStop){

        //make sure we undo the relevant switches to stop the motion based on the previous status
        if(doorStatus == opened){
          closeTheDoor();
        }else if(doorStatus == closed){
          openTheDoor();
        }
    }

    prevDoorStatus = doorStatus;

    //increase openThreshold by 1
    if(messageStr == "incrementOpenThreshold"){
      openThreshold = openThreshold + 1;
      String msg = "Set open threshold to: " + openThreshold;
      client.publish(stateTopic, msg.c_str(), true); 
    }

    //decrease openThreshold by 1
    if(messageStr == "decrementOpenThreshold"){
      openThreshold = openThreshold - 1;
      String msg = "Set open threshold to: " + openThreshold;
      client.publish(stateTopic, msg.c_str(), true); 
    }
  }
}

void getAndSendDoorStatus(){
  int doorState = analogRead(doorInput);

  //determine door state based on openThreshold
  if(doorState < openThreshold){
    statusToOpen();
  } else {
    statusToClosed();
  }

  //publish to doorStatus to HA
  //if it has changed
  //then trigger the relay if we need to
  if(prevDoorStatus != doorStatus){
    Serial.print("Door ");
    Serial.println(doorStatus);
    client.publish(stateTopic, doorStatus, true); 

    prevDoorStatus = doorStatus;
  } 
  
}

void closeTheDoor(){
  digitalWrite(relaySwitch, closeDoor);
}

void statusToClosed(){
  doorStatus = closed;
}

void openTheDoor(){
  digitalWrite(relaySwitch, openDoor);
}

void statusToOpen(){
  doorStatus = opened;  
}
