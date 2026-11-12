#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <algorithm>
#include "My_Helper.h"

WiFiClient espClient;
PubSubClient client(espClient);

constexpr unsigned long sampleIntervalMs = 200;
constexpr unsigned long calibrationMinutes = 2;
constexpr unsigned long calibrationDurationMs = calibrationMinutes * 60UL * 1000UL;
constexpr size_t samplesPerReading = 5;
constexpr unsigned long expectedReadings =
    calibrationDurationMs / (sampleIntervalMs * samplesPerReading);
constexpr unsigned long minimumReadings = expectedReadings * 9 / 10;
constexpr int minimumAdcGap = 6;
constexpr int maximumAdcReading = 1023;
constexpr char unknownCoverState[] = "None";
constexpr char calibrationPress[] = "CALIBRATE";

constexpr char calibrationFile[] = "/garage-calibration";
constexpr char temporaryCalibrationFile[] = "/garage-calibration.tmp";
constexpr uint32_t calibrationMagic = 0x47444331;

struct Thresholds {
  uint16_t openMax;
  uint16_t closedMin;
};

struct CalibrationRecord {
  uint32_t magic;
  Thresholds thresholds;
};

struct ReadingRange {
  int low = maximumAdcReading;
  int high = 0;
  unsigned long count = 0;

  void add(int reading) {
    low = min(low, reading);
    high = max(high, reading);
    ++count;
  }
};

enum class CalibrationPhase {
  Idle,
  AwaitingOpen,
  SamplingOpen,
  AwaitingClosed,
  SamplingClosed,
  Failed
};

Thresholds thresholds;
bool filesystemMounted = false;
CalibrationPhase calibrationPhase = CalibrationPhase::Idle;
ReadingRange openReadings;
ReadingRange closedReadings;
unsigned long samplingStartedAt = 0;
unsigned long lastSampleAt = 0;
int samples[samplesPerReading];
size_t sampleCount = 0;

String calibrationCommandTopic;
String calibrationStateTopic;
const char* measuredDoorStatus = nullptr;
const char* candidateDoorStatus = nullptr;
const char* coverState = unknownCoverState;
bool coverStateDirty = true;
char calibrationStatus[160] = "Ready. Press Calibrate Door to start.";
bool calibrationStatusDirty = true;

bool otaStarted = false;
unsigned long lastMqttAttemptAt = 0;
bool mqttAttempted = false;

bool readCalibration(const char* path, CalibrationRecord& record) {
  File file = LittleFS.open(path, "r");
  return file && file.size() == sizeof(record) &&
         file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) &&
         record.magic == calibrationMagic &&
         record.thresholds.openMax < record.thresholds.closedMin &&
         record.thresholds.closedMin <= maximumAdcReading;
}

void loadCalibration() {
  thresholds.openMax = constrain(openThreshold, 0, maximumAdcReading);
  thresholds.closedMin = thresholds.openMax + 1;

  LittleFS.setConfig(LittleFSConfig(false));
  filesystemMounted = LittleFS.begin();
  CalibrationRecord saved;
  if (filesystemMounted && readCalibration(calibrationFile, saved)) {
    thresholds = saved.thresholds;
  }
}

bool saveCalibration(Thresholds updated) {
  if (!filesystemMounted) {
    // Initialise the sketch's filesystem only after a successful measurement.
    LittleFS.setConfig(LittleFSConfig(true));
    filesystemMounted = LittleFS.begin();
    if (!filesystemMounted) {
      return false;
    }
  }

  const CalibrationRecord record{calibrationMagic, updated};
  File file = LittleFS.open(temporaryCalibrationFile, "w");
  if (!file) {
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  file.close();

  CalibrationRecord verified;
  // Rename replaces the previous file atomically after the new record is readable.
  if (written == sizeof(record) && readCalibration(temporaryCalibrationFile, verified) &&
      verified.thresholds.openMax == updated.openMax &&
      verified.thresholds.closedMin == updated.closedMin &&
      LittleFS.rename(temporaryCalibrationFile, calibrationFile)) {
    return true;
  }
  LittleFS.remove(temporaryCalibrationFile);
  return false;
}

void setCoverState(const char* state) {
  if (coverState != state) {
    coverState = state;
    coverStateDirty = true;
    Serial.printf("Door: %s\n", state);
  }
}

void setCalibrationStatus(const char* status) {
  snprintf(calibrationStatus, sizeof(calibrationStatus), "%s", status);
  calibrationStatusDirty = true;
}

void failCalibration(const char* reason) {
  calibrationPhase = CalibrationPhase::Failed;
  snprintf(calibrationStatus, sizeof(calibrationStatus),
           "Calibration failed. %s Press Calibrate Door to retry.", reason);
  calibrationStatusDirty = true;
}

void finishCalibrationWindow() {
  const bool samplingOpen = calibrationPhase == CalibrationPhase::SamplingOpen;
  const ReadingRange& readings = samplingOpen ? openReadings : closedReadings;
  if (readings.count < minimumReadings) {
    failCalibration("Too few readings were captured.");
    return;
  }

  if (samplingOpen) {
    calibrationPhase = CalibrationPhase::AwaitingClosed;
    setCalibrationStatus("Manually move the door to fully closed, then press Calibrate Door.");
    return;
  }

  const int gap = closedReadings.low - openReadings.high;
  if (gap < minimumAdcGap) {
    char reason[96];
    snprintf(reason, sizeof(reason), "ADC ranges too close or reversed (open %d-%d, closed %d-%d).",
             openReadings.low, openReadings.high, closedReadings.low, closedReadings.high);
    failCalibration(reason);
    return;
  }

  const Thresholds updated{
      static_cast<uint16_t>(openReadings.high + gap / 3),
      static_cast<uint16_t>(closedReadings.low - gap / 3)
  };
  if (!saveCalibration(updated)) {
    failCalibration("Could not save the thresholds to flash.");
    return;
  }

  thresholds = updated;
  calibrationPhase = CalibrationPhase::Idle;
  measuredDoorStatus = closed;
  candidateDoorStatus = nullptr;
  setCoverState(closed);
  snprintf(calibrationStatus, sizeof(calibrationStatus),
           "Calibration complete. Open at %u or below, closed at %u or above.",
           static_cast<unsigned>(thresholds.openMax), static_cast<unsigned>(thresholds.closedMin));
  calibrationStatusDirty = true;
}

void startSampling(CalibrationPhase phase) {
  calibrationPhase = phase;
  samplingStartedAt = millis();
  lastSampleAt = samplingStartedAt;
  sampleCount = 0;

  const bool samplingOpen = phase == CalibrationPhase::SamplingOpen;
  snprintf(calibrationStatus, sizeof(calibrationStatus),
           "Sampling %s readings for %lu min. Leave the door in this position.",
           samplingOpen ? "open" : "closed", calibrationMinutes);
  calibrationStatusDirty = true;
}

void handleCalibrationPress() {
  switch (calibrationPhase) {
    case CalibrationPhase::Idle:
    case CalibrationPhase::Failed:
      calibrationPhase = CalibrationPhase::AwaitingOpen;
      openReadings = ReadingRange{};
      closedReadings = ReadingRange{};
      measuredDoorStatus = nullptr;
      candidateDoorStatus = nullptr;
      setCoverState(unknownCoverState);
      setCalibrationStatus("Manually move the door to fully open, then press Calibrate Door.");
      break;
    case CalibrationPhase::AwaitingOpen:
      startSampling(CalibrationPhase::SamplingOpen);
      break;
    case CalibrationPhase::AwaitingClosed:
      startSampling(CalibrationPhase::SamplingClosed);
      break;
    case CalibrationPhase::SamplingOpen:
    case CalibrationPhase::SamplingClosed:
      break;
  }
}

void publishDebugReading(int initial, int secondary, int median) {
  if (!client.connected()) {
    return;
  }
  StaticJsonDocument<256> debug;
  debug["initial"] = initial;
  debug["secondary"] = secondary;
  debug["median"] = median;
  debug["openThreshold"] = thresholds.openMax;
  debug["closedThreshold"] = thresholds.closedMin;
  char json[160];
  serializeJson(debug, json);
  client.publish(coverDebugTopic, json, false);
}

void updateDoorStatus(int reading) {
  const char* observedStatus = nullptr;
  if (reading <= thresholds.openMax) {
    observedStatus = opened;
  } else if (reading >= thresholds.closedMin) {
    observedStatus = closed;
  }

  if (observedStatus && observedStatus == candidateDoorStatus &&
      observedStatus != measuredDoorStatus) {
    measuredDoorStatus = observedStatus;
    setCoverState(observedStatus);
  }
  candidateDoorStatus = observedStatus;
}

void sampleDoor(unsigned long now) {
  const bool samplingOpen = calibrationPhase == CalibrationPhase::SamplingOpen;
  const bool samplingClosed = calibrationPhase == CalibrationPhase::SamplingClosed;
  const bool calibrating = samplingOpen || samplingClosed;
  if (!calibrating && calibrationPhase != CalibrationPhase::Idle) {
    return;
  }

  const unsigned long elapsed = now - samplingStartedAt;
  // A delayed loop must not include readings from after the sampling window.
  if ((!calibrating || elapsed <= calibrationDurationMs) &&
      now - lastSampleAt >= sampleIntervalMs) {
    lastSampleAt = now;
    samples[sampleCount++] = analogRead(doorInput);
    if (sampleCount == samplesPerReading) {
      const int initial = samples[0];
      const int secondary = samples[samplesPerReading - 1];
      std::sort(samples, samples + samplesPerReading);
      const int median = samples[samplesPerReading / 2];
      sampleCount = 0;

      if (calibrating) {
        (samplingOpen ? openReadings : closedReadings).add(median);
      } else {
        updateDoorStatus(median);
      }
      publishDebugReading(initial, secondary, median);
    }
  }

  if (calibrating && elapsed >= calibrationDurationMs) {
    sampleCount = 0;
    finishCalibrationWindow();
  }
}

void adjustThresholds(int change) {
  const int openMax = thresholds.openMax + change;
  const int closedMin = thresholds.closedMin + change;
  if (openMax >= 0 && closedMin <= maximumAdcReading + 1) {
    thresholds.openMax = openMax;
    thresholds.closedMin = closedMin;
    candidateDoorStatus = nullptr;
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  const auto isPayload = [message, length](const char* expected) {
    return length == strlen(expected) && memcmp(message, expected, length) == 0;
  };
  if (calibrationCommandTopic == topic) {
    if (isPayload(calibrationPress)) {
      handleCalibrationPress();
    }
    return;
  }

  if (strcmp(topic, commandTopic) != 0 || calibrationPhase != CalibrationPhase::Idle) {
    return;
  }

  if ((isPayload(payloadOpen) && measuredDoorStatus != opened) || isPayload("forceOpen")) {
    if (measuredDoorStatus == closed) {
      digitalWrite(relaySwitch, closeDoor);
      delay(500);
    }
    setCoverState(opening);
    digitalWrite(relaySwitch, openDoor);
  } else if ((isPayload(payloadClose) && measuredDoorStatus != closed) || isPayload("forceClose")) {
    if (measuredDoorStatus == opened) {
      digitalWrite(relaySwitch, openDoor);
      delay(500);
    }
    setCoverState(closing);
    digitalWrite(relaySwitch, closeDoor);
  } else if (isPayload(payloadStop)) {
    if (measuredDoorStatus == opened) {
      digitalWrite(relaySwitch, closeDoor);
    } else if (measuredDoorStatus == closed) {
      digitalWrite(relaySwitch, openDoor);
    }
  } else if (isPayload("incrementOpenThreshold")) {
    adjustThresholds(1);
  } else if (isPayload("decrementOpenThreshold")) {
    adjustThresholds(-1);
  }
}

void addDiscoveryDevice(JsonDocument& config) {
  config["avty_t"] = availabilityTopic;
  config["pl_avail"] = payloadAvailable;
  config["pl_not_avail"] = payloadNotAvailable;
  config["dev"]["name"] = mqttDeviceClientId;
  config["dev"]["mf"] = manufacturer;
  config["dev"]["mdl"] = model;
  config["dev"]["sw"] = softwareVersion;
  config["dev"]["ids"][0] = mqttDeviceClientId;
}

bool publishDiscovery(const char* topic, const JsonDocument& config) {
  char json[896];
  if (config.overflowed() || measureJson(config) >= sizeof(json)) {
    return false;
  }
  serializeJson(config, json);
  return client.publish(topic, json, true);
}

bool sendConfigDetailsToHA() {
  StaticJsonDocument<1024> config;
  addDiscoveryDevice(config);
  config["name"] = mqttDeviceName;
  config["dev_cla"] = mqttDeviceClass;
  config["stat_t"] = stateTopic;
  config["cmd_t"] = commandTopic;
  config["stat_open"] = opened;
  config["stat_clsd"] = closed;
  config["stat_closing"] = closing;
  config["stat_opening"] = opening;
  config["pl_open"] = payloadOpen;
  config["pl_cls"] = payloadClose;
  config["pl_stop"] = payloadStop;
  config["opt"] = false;
  config["ret"] = false;
  config["uniq_id"] = mqttDeviceClientId;
  if (!publishDiscovery(configTopic, config)) {
    return false;
  }

  const String coverConfigTopic(configTopic);
  const int prefixEnd = coverConfigTopic.indexOf('/');
  const String discoveryPrefix = prefixEnd > 0 ? coverConfigTopic.substring(0, prefixEnd) : "homeassistant";
  const String calibrationId = mqttDeviceClientId + "_calibration";

  config.clear();
  addDiscoveryDevice(config);
  config["name"] = "Calibrate Door";
  config["cmd_t"] = calibrationCommandTopic;
  config["pl_prs"] = calibrationPress;
  config["ret"] = false;
  config["uniq_id"] = calibrationId + "_button";
  const String buttonConfigTopic = discoveryPrefix + "/button/" + calibrationId + "/config";
  if (!publishDiscovery(buttonConfigTopic.c_str(), config)) {
    return false;
  }

  config.clear();
  addDiscoveryDevice(config);
  config["name"] = "Door Calibration";
  config["stat_t"] = calibrationStateTopic;
  config["uniq_id"] = calibrationId + "_status";
  const String sensorConfigTopic = discoveryPrefix + "/sensor/" + calibrationId + "/config";
  return publishDiscovery(sensorConfigTopic.c_str(), config);
}

bool publishStates() {
  if (coverStateDirty && client.publish(stateTopic, coverState, true)) {
    coverStateDirty = false;
  }
  if (calibrationStatusDirty && client.publish(calibrationStateTopic.c_str(), calibrationStatus, true)) {
    calibrationStatusDirty = false;
  }
  return !coverStateDirty && !calibrationStatusDirty;
}

void connectClient(unsigned long now) {
  if (mqttAttempted && now - lastMqttAttemptAt < 5000) {
    return;
  }

  mqttAttempted = true;
  const bool connected = client.connect(mqttDeviceClientId.c_str(), mqtt_user, mqtt_password,
                                        availabilityTopic, 0, true, payloadNotAvailable);
  lastMqttAttemptAt = millis();
  if (!connected) {
    Serial.printf("MQTT connection failed: %d\n", client.state());
    return;
  }

  coverStateDirty = true;
  calibrationStatusDirty = true;
  // Older discovery retained cover commands. Clear any stored commands before subscribing.
  if (!client.publish(commandTopic, "", true) ||
      !client.publish(calibrationCommandTopic.c_str(), "", true) ||
      !client.subscribe(commandTopic) ||
      !client.subscribe(calibrationCommandTopic.c_str()) ||
      !sendConfigDetailsToHA() ||
      !publishStates() ||
      !client.publish(availabilityTopic, payloadAvailable, true)) {
    Serial.println("Could not initialise MQTT topics.");
    client.disconnect();
  }
}

void setup() {
  pinMode(relaySwitch, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(doorInput, INPUT);
  Serial.begin(115200);

  loadCalibration();
  calibrationCommandTopic = String(commandTopic) + "/calibration";
  calibrationStateTopic = String(stateTopic) + "/calibration";

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setBufferSize(1024);
  client.setSocketTimeout(1);

  ArduinoOTA.setHostname((mqttDeviceClientId + "-" + String(ESP.getChipId())).c_str());
  ArduinoOTA.onStart([]() { Serial.println("OTA started"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA finished"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total > 0) {
      Serial.printf("Progress: %u%%\r", progress * 100 / total);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error: %u\n", error); });
}

void loop() {
  const unsigned long now = millis();
  sampleDoor(now);

  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledPin, LOW);
    return;
  }

  if (!otaStarted) {
    ArduinoOTA.begin();
    otaStarted = true;
  }
  ArduinoOTA.handle();

  if (client.connected()) {
    digitalWrite(ledPin, HIGH);
    client.loop();
    publishStates();
  } else {
    digitalWrite(ledPin, (now / 500) % 2 == 0 ? HIGH : LOW);
    connectClient(now);
  }
}
