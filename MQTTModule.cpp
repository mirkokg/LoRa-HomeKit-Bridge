/*
 * MQTTModule.cpp - MQTT Client Module Implementation
 */

#include "network/MQTTModule.h"
#include "data/Settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HomeSpan.h>

// External variables for diagnostics
extern unsigned long boot_time;
extern uint32_t packets_received;
extern bool homekit_started;
extern int getActiveDeviceCount();

// Global objects
WiFiClient mqttWifiClient;
WiFiClientSecure mqttSecureClient;
PubSubClient mqttClient;
unsigned long lastMqttReconnect = 0;

// Rate limiting for diagnostics publishing
unsigned long lastDiagnosticsPublish = 0;
#define DIAGNOSTICS_MIN_INTERVAL 30000  // Minimum 30 seconds between publishes

#define MQTT_RECONNECT_INTERVAL 5000
#define MQTT_BUFFER_SIZE 1024

// Topics
String bridgeStatusTopic;
String bridgeLwtTopic;

// Helper to get gateway MAC without colons
String getGatewayMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

// Helper to build topic with prefix
String buildTopic(String topic) {
  return String(mqtt_topic_prefix) + "/" + topic;
}

// MQTT callback for incoming messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr;

  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }

  Serial.printf("[MQTT] Message received on %s: %s\n", topic, payloadStr.c_str());

  // Handle subscribed topics here
  // Example: if (topicStr.endsWith("/set")) { ... }
}

// Initialize MQTT client
void initMQTT() {
  if (!mqtt_enabled || strlen(mqtt_server) == 0) {
    return;
  }

  // Set up client based on SSL/TLS setting
  if (mqtt_ssl_enabled) {
    mqttSecureClient.setInsecure(); // Accept all certificates (for simplicity)
    mqttClient.setClient(mqttSecureClient);
  } else {
    mqttClient.setClient(mqttWifiClient);
  }

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setCallback(mqttCallback);

  // Build topic strings
  String gatewayMac = getGatewayMac();
  bridgeStatusTopic = buildTopic("bridge/" + gatewayMac + "/status");
  bridgeLwtTopic = bridgeStatusTopic;

  Serial.printf("[MQTT] Configured for %s:%d (SSL: %s, QoS: %d)\n",
                mqtt_server, mqtt_port,
                mqtt_ssl_enabled ? "Yes" : "No",
                mqtt_qos);
}

// Connect to MQTT broker
void connectMQTT() {
  if (!mqtt_enabled || strlen(mqtt_server) == 0) {
    return;
  }

  Serial.print("[MQTT] Connecting to broker...");

  // Generate unique client ID using MAC address
  String clientId = "lora-bridge-";
  clientId += WiFi.macAddress();
  clientId.replace(":", "");

  bool connected = false;

  // Connect with LWT (Last Will and Testament)
  if (strlen(mqtt_username) > 0 && strlen(mqtt_password) > 0) {
    connected = mqttClient.connect(
      clientId.c_str(),
      mqtt_username,
      mqtt_password,
      bridgeLwtTopic.c_str(),  // LWT topic
      mqtt_qos,                 // LWT QoS
      mqtt_retain,              // LWT retain
      "offline"                 // LWT message
    );
  } else {
    connected = mqttClient.connect(
      clientId.c_str(),
      bridgeLwtTopic.c_str(),   // LWT topic
      mqtt_qos,                 // LWT QoS
      mqtt_retain,              // LWT retain
      "offline"                 // LWT message
    );
  }

  if (connected) {
    Serial.println(" Connected!");
    Serial.printf("[MQTT] Buffer size: %d bytes\n", mqttClient.getBufferSize());

    // Publish online status
    publishBridgeStatus(true);

    // Subscribe to command topics if needed
    // String commandTopic = buildTopic("bridge/" + getGatewayMac() + "/set");
    // mqttClient.subscribe(commandTopic.c_str(), mqtt_qos);

    // IMPORTANT: Publish gateway discovery FIRST before device discoveries
    // This is critical for Home Assistant to show devices under "Connected devices"
    publishGatewayDiscovery();

    // Republish discovery for all existing devices (order matters!)
    // This ensures devices loaded from flash are properly linked to the gateway
    extern Device devices[];
    extern int device_count;
    for (int i = 0; i < device_count; i++) {
      if (devices[i].active) {
        publishHomeAssistantDiscovery(&devices[i], devices[i].id);
        delay(100);  // Small delay between discoveries to avoid overwhelming MQTT
      }
    }

    // Publish initial bridge diagnostics
    // Note: HomeKit pairing status may not be accurate yet if HomeSpan is still loading
    // The main loop will detect pairing status changes and republish
    publishBridgeDiagnostics();
  } else {
    Serial.printf(" Failed, rc=%d\n", mqttClient.state());
  }
}

// Disconnect from MQTT broker gracefully
void disconnectMQTT() {
  if (mqttClient.connected()) {
    publishBridgeStatus(false);
    mqttClient.disconnect();
    Serial.println("[MQTT] Disconnected");
  }
}

// Reconnect to MQTT broker with interval throttling
void reconnectMQTT() {
  if (!mqtt_enabled || mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
    lastMqttReconnect = now;
    connectMQTT();
  }
}

// Helper to check connection status
bool isMQTTConnected() {
  if (!mqtt_enabled)
    return false;
  return mqttClient.connected();
}

// MQTT loop - call in main loop
void loopMQTT() {
  if (!mqtt_enabled) {
    return;
  }

  if (!mqttClient.connected()) {
    reconnectMQTT();
  } else {
    mqttClient.loop();
  }
}

// Test MQTT connection (for web UI test button)
bool testMQTTConnection(const char *server, uint16_t port, const char *username,
                        const char *password) {
  WiFiClient testWifiClient;
  PubSubClient testClient(testWifiClient);
  testClient.setServer(server, port);

  Serial.printf("[MQTT] Testing connection to %s:%d\n", server, port);

  bool connected = false;
  if (strlen(username) > 0 && strlen(password) > 0) {
    connected = testClient.connect("lora-bridge-test", username, password);
  } else {
    connected = testClient.connect("lora-bridge-test");
  }

  if (connected) {
    Serial.println("[MQTT] Test connection successful");
    testClient.disconnect();
  } else {
    Serial.printf("[MQTT] Test connection failed, rc=%d\n", testClient.state());
  }

  return connected;
}

// Publish bridge online/offline status
void publishBridgeStatus(bool online) {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  const char* status = online ? "online" : "offline";
  bool success = mqttClient.publish(bridgeStatusTopic.c_str(), status, mqtt_retain);

  if (success) {
    Serial.printf("[MQTT] Published bridge status: %s\n", status);
  } else {
    Serial.printf("[MQTT] Failed to publish bridge status\n");
  }
}

// Publish bridge diagnostics (uptime, wifi signal, etc.)
void publishBridgeDiagnostics() {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  String gatewayMac = getGatewayMac();
  String diagnosticTopic = buildTopic("bridge/" + gatewayMac + "/diagnostics");

  // Check if HomeKit is paired
  bool isPaired = homekit_started && (homeSpan.controllerListBegin() != homeSpan.controllerListEnd());

  // Build JSON payload with comprehensive diagnostics
  String payload = "{";

  // WiFi information
  payload += "\"wifi\":{";
  payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  payload += "\"ssid\":\"" + String(wifi_ssid) + "\",";
  payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  payload += "\"mac\":\"" + WiFi.macAddress() + "\"";
  payload += "},";

  // LoRa radio information
  payload += "\"lora\":{";
  payload += "\"frequency\":" + String(lora_frequency, 2) + ",";
  payload += "\"spreading_factor\":" + String(lora_sf) + ",";
  payload += "\"bandwidth\":" + String(lora_bw);
  payload += "},";

  // Statistics
  payload += "\"stats\":{";
  payload += "\"packets_received\":" + String(packets_received) + ",";
  payload += "\"active_devices\":" + String(getActiveDeviceCount()) + ",";
  payload += "\"uptime\":" + String((millis() - boot_time) / 1000);
  payload += "},";

  // HomeKit status
  payload += "\"homekit\":{";
  payload += "\"paired\":" + String(isPaired ? "true" : "false");
  payload += "},";

  // MQTT status
  payload += "\"mqtt\":{";
  payload += "\"connected\":true,";
  payload += "\"broker\":\"" + String(mqtt_server) + "\"";
  payload += "},";

  // System information
  payload += "\"system\":{";
  payload += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  payload += "\"heap_size\":" + String(ESP.getHeapSize());
  payload += "}";

  payload += "}";

  bool success = mqttClient.publish(diagnosticTopic.c_str(), payload.c_str(), mqtt_retain);

  if (success) {
    Serial.println("[MQTT] Published bridge diagnostics");
    lastDiagnosticsPublish = millis();  // Update timestamp
  } else {
    Serial.println("[MQTT] Failed to publish diagnostics");
  }
}

// Publish diagnostics only if minimum interval has passed (rate-limited)
void publishBridgeDiagnosticsIfChanged() {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastDiagnosticsPublish >= DIAGNOSTICS_MIN_INTERVAL) {
    publishBridgeDiagnostics();
  }
}

// Publish Home Assistant auto-discovery for gateway sensors
void publishGatewayDiscovery() {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  String gatewayMac = getGatewayMac();
  String uniqueId = "lora_bridge_" + gatewayMac;

  Serial.println("[MQTT] Publishing gateway auto-discovery");

  // Device info JSON - shared across all gateway sensors
  String deviceInfo =
      String("\"device\":{\"identifiers\":[\"lora_gateway_") + gatewayMac + "\"],\"name\":\"LoRa Gateway " +
      gatewayMac.substring(gatewayMac.length() - 4) +
      "\",\"manufacturer\":\"ESP32\",\"model\":\"TTGO-LoRa32\",\"sw_version\":\"2.0\"}";

  // WiFi RSSI sensor
  String rssiTopic = buildTopic("sensor/" + uniqueId + "/wifi_rssi/config");
  String rssiPayload = "{\"name\":\"WiFi Signal\",\"unique_id\":\"" + uniqueId +
                       "_wifi_rssi\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                       "\",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\"," +
                       "\"state_class\":\"measurement\",\"value_template\":\"{{ value_json.wifi.rssi }}\"," +
                       "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(rssiTopic.c_str(), rssiPayload.c_str(), mqtt_retain);

  // Packets received sensor
  String packetsTopic = buildTopic("sensor/" + uniqueId + "/packets/config");
  String packetsPayload = "{\"name\":\"Packets Received\",\"unique_id\":\"" + uniqueId +
                          "_packets\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                          "\",\"state_class\":\"total_increasing\",\"icon\":\"mdi:package-variant\"," +
                          "\"value_template\":\"{{ value_json.stats.packets_received }}\"," +
                          "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(packetsTopic.c_str(), packetsPayload.c_str(), mqtt_retain);

  // Active devices sensor
  String devicesTopic = buildTopic("sensor/" + uniqueId + "/active_devices/config");
  String devicesPayload = "{\"name\":\"Active Devices\",\"unique_id\":\"" + uniqueId +
                          "_devices\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                          "\",\"state_class\":\"measurement\",\"icon\":\"mdi:devices\"," +
                          "\"value_template\":\"{{ value_json.stats.active_devices }}\"," +
                          deviceInfo + "}";
  mqttClient.publish(devicesTopic.c_str(), devicesPayload.c_str(), mqtt_retain);

  // Uptime sensor
  String uptimeTopic = buildTopic("sensor/" + uniqueId + "/uptime/config");
  String uptimePayload = "{\"name\":\"Uptime\",\"unique_id\":\"" + uniqueId +
                         "_uptime\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                         "\",\"unit_of_measurement\":\"s\",\"device_class\":\"duration\"," +
                         "\"state_class\":\"total_increasing\",\"value_template\":\"{{ value_json.stats.uptime }}\"," +
                         "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(uptimeTopic.c_str(), uptimePayload.c_str(), mqtt_retain);

  // Free heap sensor
  String heapTopic = buildTopic("sensor/" + uniqueId + "/free_heap/config");
  String heapPayload = "{\"name\":\"Free Memory\",\"unique_id\":\"" + uniqueId +
                       "_heap\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                       "\",\"unit_of_measurement\":\"B\",\"device_class\":\"data_size\"," +
                       "\"state_class\":\"measurement\",\"value_template\":\"{{ value_json.system.free_heap }}\"," +
                       "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(heapTopic.c_str(), heapPayload.c_str(), mqtt_retain);

  // IP address sensor
  String ipTopic = buildTopic("sensor/" + uniqueId + "/ip_address/config");
  String ipPayload = "{\"name\":\"IP Address\",\"unique_id\":\"" + uniqueId +
                     "_ip\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                     "\",\"icon\":\"mdi:ip-network\"," +
                     "\"value_template\":\"{{ value_json.wifi.ip }}\"," +
                     "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(ipTopic.c_str(), ipPayload.c_str(), mqtt_retain);

  // HomeKit paired binary sensor
  String pairedTopic = buildTopic("binary_sensor/" + uniqueId + "/homekit_paired/config");
  String pairedPayload = "{\"name\":\"HomeKit Paired\",\"unique_id\":\"" + uniqueId +
                         "_paired\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                         "\",\"payload_on\":\"true\",\"payload_off\":\"false\"," +
                         "\"value_template\":\"{{ value_json.homekit.paired | string | lower }}\"," +
                         "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(pairedTopic.c_str(), pairedPayload.c_str(), mqtt_retain);

  // LoRa frequency sensor
  String freqTopic = buildTopic("sensor/" + uniqueId + "/lora_frequency/config");
  String freqPayload = "{\"name\":\"LoRa Frequency\",\"unique_id\":\"" + uniqueId +
                       "_frequency\",\"state_topic\":\"" + buildTopic("bridge/" + gatewayMac + "/diagnostics") +
                       "\",\"unit_of_measurement\":\"MHz\",\"icon\":\"mdi:radio-tower\"," +
                       "\"value_template\":\"{{ value_json.lora.frequency }}\"," +
                       "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
  mqttClient.publish(freqTopic.c_str(), freqPayload.c_str(), mqtt_retain);

  Serial.println("[MQTT] Gateway auto-discovery published");
}

// Publish Home Assistant auto-discovery configuration for a device
void publishHomeAssistantDiscovery(Device *dev, const char *deviceId) {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  String gatewayMac = getGatewayMac();
  String uniquePrefix = gatewayMac + "_" + String(deviceId);

  Serial.printf("[MQTT] Publishing auto-discovery for device: %s\n", deviceId);

  // Availability topic (shared across all entities)
  String availabilityTopic = buildTopic("sensor/" + uniquePrefix + "/availability");

  // Device info JSON - shared across all sensors
  // Use via_device to show sensors under the gateway device in Home Assistant
  String deviceInfo =
      String("\"device\":{\"identifiers\":[\"") + deviceId + "\"],\"name\":\"" +
      String(dev->name) +
      "\",\"manufacturer\":\"LoRa Sensor\",\"model\":\"LoRa-v1\",\"via_device\":\"lora_gateway_" +
      gatewayMac + "\"}";

  // Availability JSON - shared across all entities
  String availability = "\"availability\":{\"topic\":\"" + availabilityTopic +
                       "\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"}";

  // Temperature sensor
  if (dev->has_temp) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/temperature/config");
    String payload =
        "{\"name\":\"Temperature\",\"unique_id\":\"" + uniquePrefix +
        "_temp\",\"state_topic\":\"" + buildTopic("sensor/" + uniquePrefix + "/temperature") +
        "\",\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\"," +
        "\"state_class\":\"measurement\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish temperature discovery\n");
    }
  }

  // Humidity sensor
  if (dev->has_hum) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/humidity/config");
    String payload =
        "{\"name\":\"Humidity\",\"unique_id\":\"" + uniquePrefix +
        "_hum\",\"state_topic\":\"" + buildTopic("sensor/" + uniquePrefix + "/humidity") +
        "\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\"," +
        "\"state_class\":\"measurement\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish humidity discovery\n");
    }
  }

  // Battery sensor
  if (dev->has_batt) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/battery/config");
    String payload =
        "{\"name\":\"Battery\",\"unique_id\":\"" + uniquePrefix +
        "_batt\",\"state_topic\":\"" + buildTopic("sensor/" + uniquePrefix + "/battery") +
        "\",\"unit_of_measurement\":\"%\",\"device_class\":\"battery\"," +
        "\"state_class\":\"measurement\"," +
        "\"entity_category\":\"diagnostic\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish battery discovery\n");
    }
  }

  // Light sensor
  if (dev->has_light) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/lux/config");
    String payload =
        "{\"name\":\"Illuminance\",\"unique_id\":\"" + uniquePrefix +
        "_lux\",\"state_topic\":\"" + buildTopic("sensor/" + uniquePrefix + "/lux") +
        "\",\"unit_of_measurement\":\"lx\",\"device_class\":\"illuminance\"," +
        "\"state_class\":\"measurement\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish lux discovery\n");
    }
  }

  // Motion sensor (binary sensor)
  if (dev->has_motion) {
    String topic = buildTopic("binary_sensor/" + uniquePrefix + "/motion/config");
    String payload =
        "{\"name\":\"Motion\",\"unique_id\":\"" + uniquePrefix +
        "_motion\",\"state_topic\":\"" + buildTopic("binary_sensor/" + uniquePrefix + "/motion") +
        "\",\"device_class\":\"motion\",\"payload_on\":\"on\",\"payload_off\":\"off\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish motion discovery\n");
    }
  }

  // Contact sensor (binary sensor)
  if (dev->has_contact) {
    String sensorType = (dev->contact_type == 1) ? "door" : "window";
    String topic = buildTopic("binary_sensor/" + uniquePrefix + "/contact/config");
    String payload =
        "{\"name\":\"Contact\",\"unique_id\":\"" + uniquePrefix +
        "_contact\",\"state_topic\":\"" + buildTopic("binary_sensor/" + uniquePrefix + "/contact") +
        "\",\"device_class\":\"" + sensorType +
        "\",\"payload_on\":\"on\",\"payload_off\":\"off\"," +
        availability + "," + deviceInfo + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish contact discovery\n");
    }
  }

  // RSSI diagnostic sensor
  String rssiTopic = buildTopic("sensor/" + uniquePrefix + "/rssi/config");
  String rssiPayload = "{\"name\":\"RSSI\",\"unique_id\":\"" + uniquePrefix +
                       "_rssi\",\"state_topic\":\"" + buildTopic("sensor/" + uniquePrefix + "/rssi") +
                       "\",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\"," +
                       "\"state_class\":\"measurement\"," +
                       "\"entity_category\":\"diagnostic\"," +
                       availability + "," + deviceInfo + "}";

  if (!mqttClient.publish(rssiTopic.c_str(), rssiPayload.c_str(), mqtt_retain)) {
    Serial.printf("[MQTT] Failed to publish RSSI discovery\n");
  }

  // Publish initial availability as online
  if (!mqttClient.publish(availabilityTopic.c_str(), "online", mqtt_retain)) {
    Serial.printf("[MQTT] Failed to publish availability\n");
  }

  Serial.printf("[MQTT] Auto-discovery published for %s\n", deviceId);
}

// Publish device sensor data
void publishDeviceData(Device *dev, JsonDocument &doc, int rssi) {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  String gatewayMac = getGatewayMac();
  String uniquePrefix = gatewayMac + "_" + String(dev->id);

  // Publish temperature
  if (doc.containsKey("t") && dev->has_temp) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/temperature");
    String value = String(doc["t"].as<float>(), 1);
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish temperature\n");
    }
  }

  // Publish humidity
  if (doc.containsKey("hu") && dev->has_hum) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/humidity");
    String value = String(doc["hu"].as<float>(), 0);
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish humidity\n");
    }
  }

  // Publish battery
  if (doc.containsKey("b") && dev->has_batt) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/battery");
    String value = String(doc["b"].as<int>());
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish battery\n");
    }
  }

  // Publish light/lux
  if (doc.containsKey("lux") && dev->has_light) {
    String topic = buildTopic("sensor/" + uniquePrefix + "/lux");
    String value = String(doc["lux"].as<int>());
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish lux\n");
    }
  }

  // Publish motion (binary sensor)
  if (doc.containsKey("m") && dev->has_motion) {
    String topic = buildTopic("binary_sensor/" + uniquePrefix + "/motion");
    String value = doc["m"].as<bool>() ? "on" : "off";
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish motion\n");
    }
  }

  // Publish contact (binary sensor)
  if (doc.containsKey("c") && dev->has_contact) {
    String topic = buildTopic("binary_sensor/" + uniquePrefix + "/contact");
    String value = doc["c"].as<bool>() ? "on" : "off";
    if (!mqttClient.publish(topic.c_str(), value.c_str(), mqtt_retain)) {
      Serial.printf("[MQTT] Failed to publish contact\n");
    }
  }

  // Publish RSSI
  String rssiTopic = buildTopic("sensor/" + uniquePrefix + "/rssi");
  String rssiValue = String(rssi);
  if (!mqttClient.publish(rssiTopic.c_str(), rssiValue.c_str(), mqtt_retain)) {
    Serial.printf("[MQTT] Failed to publish RSSI\n");
  }
}

// Remove device from MQTT (publish empty configs to remove from Home Assistant)
void removeDeviceFromMQTT(const char *deviceId) {
  if (!mqtt_enabled || !mqttClient.connected()) {
    return;
  }

  String gatewayMac = getGatewayMac();
  String uniquePrefix = gatewayMac + "_" + String(deviceId);

  Serial.printf("[MQTT] Removing device from MQTT: %s\n", deviceId);

  // Publish empty payloads to remove entities from Home Assistant
  String topics[] = {
    buildTopic("sensor/" + uniquePrefix + "/temperature/config"),
    buildTopic("sensor/" + uniquePrefix + "/humidity/config"),
    buildTopic("sensor/" + uniquePrefix + "/battery/config"),
    buildTopic("sensor/" + uniquePrefix + "/lux/config"),
    buildTopic("binary_sensor/" + uniquePrefix + "/motion/config"),
    buildTopic("binary_sensor/" + uniquePrefix + "/contact/config"),
    buildTopic("sensor/" + uniquePrefix + "/rssi/config")
  };

  for (String topic : topics) {
    if (!mqttClient.publish(topic.c_str(), "", mqtt_retain)) {
      Serial.printf("[MQTT] Failed to remove config: %s\n", topic.c_str());
    }
  }

  // Mark availability as offline
  String availabilityTopic = buildTopic("sensor/" + uniquePrefix + "/availability");
  mqttClient.publish(availabilityTopic.c_str(), "offline", mqtt_retain);

  Serial.printf("[MQTT] Device removed from MQTT: %s\n", deviceId);
}
