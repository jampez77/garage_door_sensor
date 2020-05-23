
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    String clientId = "GarageDoorSensor-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.publish(stateTopic, "online", true);  
      client.subscribe(subTopic);
      Serial.println("Subscribed to: ");
      Serial.println(subTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {

  pinMode(channel1RelaySwitch, OUTPUT);
  pinMode(channel2RelaySwitch, OUTPUT);

  digitalWrite(channel1RelaySwitch, LOW);
  digitalWrite(channel2RelaySwitch, LOW);
  
  Serial.begin(115200);
  // put your setup code here, to run once:
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  while (!client.connected()) {
    reconnect();
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void callback(char* topic, byte* message, unsigned int length) {

  String messageStr;
  
  for (int i = 0; i < length; i++) {
    messageStr += (char)message[i];
  }
  
  if (String(topic) == subTopic) {
     Serial.print("Home Assistant Command: ");
     Serial.println(messageStr);

      int channel1RelaySwitchInput = digitalRead(channel1RelaySwitch);
      int channel2RelaySwitchInput = digitalRead(channel2RelaySwitch);

     if(messageStr == OPEN || messageStr == "testOpen" || messageStr == "testBothHigh"){
       digitalWrite(channel1RelaySwitch, HIGH);
       digitalWrite(channel2RelaySwitch, HIGH);
       doorStatus = opened;
     }else if(messageStr == CLOSE || messageStr == "testClose" || messageStr == "testBothLow"){
      digitalWrite(channel1RelaySwitch, LOW);
      digitalWrite(channel2RelaySwitch, LOW);
      doorStatus = closed;
     }else if(messageStr == STOP || messageStr == "testStop"){

        //make sure we undo the relevant switches to stop the motion based on the previous status
        if(doorStatus == opened){
          digitalWrite(channel1RelaySwitch, LOW);
          digitalWrite(channel2RelaySwitch, LOW);
        }else if(doorStatus == closed){
          digitalWrite(channel1RelaySwitch, HIGH);
          digitalWrite(channel2RelaySwitch, HIGH);
        }
      
    }


    Serial.print("Previous Door Status: ");
    Serial.println(prevDoorStatus);

    Serial.print("Door Status: ");
    Serial.println(doorStatus);

    //if publish any door status changes
    if(prevDoorStatus != doorStatus){
      Serial.print("MQTT Door Status: ");
      Serial.println(doorStatus);

      //announce transitionaing status before waiting to annouce open/closed status
      if(doorStatus == closed){
        client.publish(stateTopic, closing, true); 
      }else if (doorStatus == opened){
        client.publish(stateTopic, opening, true); 
      }

      delay(3000);
      client.publish(stateTopic, doorStatus, true); 
    }
    
    prevDoorStatus = doorStatus;

    if(messageStr == "test1Low"){
       digitalWrite(channel1RelaySwitch, LOW);
    }

    if(messageStr == "test2Low"){
       digitalWrite(channel2RelaySwitch, LOW);
    }

    if(messageStr == "test1High"){
       digitalWrite(channel1RelaySwitch, HIGH);
    }

    if(messageStr == "test2High"){
       digitalWrite(channel2RelaySwitch, HIGH);
    }
    
  }
}
