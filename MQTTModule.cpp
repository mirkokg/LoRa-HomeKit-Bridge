/*
 * MQTTModule.cpp - MQTT Client Module Implementation
 */

#include "network/MQTTModule.h"
#include "data/Settings.h"
#include <WiFi.h>

// Global objects
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
unsigned long lastMqttReconnect = 0;

#define MQTT_RETAIN true
#define MQTT_RECONNECT_INTERVAL 5000

// Initialize MQTT client
void initMQTT() {
    if (mqtt_enabled && strlen(mqtt_server) > 0) {
        mqttClient.setServer(mqtt_server, mqtt_port);
        mqttClient.setBufferSize(1024);  // Increase buffer for discovery payloads
        Serial.printf("[MQTT] Configured for %s:%d\n", mqtt_server, mqtt_port);
    }
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
    if (strlen(mqtt_username) > 0 && strlen(mqtt_password) > 0) {
        connected = mqttClient.connect(clientId.c_str(), mqtt_username, mqtt_password);
    } else {
        connected = mqttClient.connect(clientId.c_str());
    }

    if (connected) {
        Serial.println(" Connected!");
        Serial.printf("[MQTT] Buffer size: %d bytes\n", mqttClient.getBufferSize());
    } else {
        Serial.printf(" Failed, rc=%d\n", mqttClient.state());
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
bool testMQTTConnection(const char* server, uint16_t port, const char* username, const char* password) {
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

// Helper function to get gateway MAC without colons
String getGatewayMac() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    return mac;
}

// Publish Home Assistant auto-discovery configuration for a device
void publishHomeAssistantDiscovery(Device* dev, const char* deviceId) {
    if (!mqtt_enabled || !mqttClient.connected()) {
        return;
    }

    String gatewayMac = getGatewayMac();
    String uniquePrefix = gatewayMac + "_" + String(deviceId);

    Serial.printf("[MQTT] Publishing auto-discovery for device: %s\n", deviceId);

    // Device info JSON - shared across all sensors
    String deviceInfo = String("\"device\":{\"identifiers\":[\"") + deviceId +
                       "\"],\"name\":\"" + String(dev->name) +
                       "\",\"manufacturer\":\"LoRa Sensor\",\"model\":\"LoRa-v1\"}";

    // Temperature sensor
    if (dev->has_temp) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/temperature/config";
        String payload = "{\"name\":\"Temperature\",\"unique_id\":\"" + uniquePrefix +
                        "_temp\",\"state_topic\":\"homeassistant/sensor/" + uniquePrefix +
                        "/temperature/state\",\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\"," +
                        deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // Humidity sensor
    if (dev->has_hum) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/humidity/config";
        String payload = "{\"name\":\"Humidity\",\"unique_id\":\"" + uniquePrefix +
                        "_hum\",\"state_topic\":\"homeassistant/sensor/" + uniquePrefix +
                        "/humidity/state\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\"," +
                        deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // Battery sensor
    if (dev->has_batt) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/battery/config";
        String payload = "{\"name\":\"Battery\",\"unique_id\":\"" + uniquePrefix +
                        "_batt\",\"state_topic\":\"homeassistant/sensor/" + uniquePrefix +
                        "/battery/state\",\"unit_of_measurement\":\"%\",\"device_class\":\"battery\"," +
                        "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // Light sensor
    if (dev->has_light) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/lux/config";
        String payload = "{\"name\":\"Illuminance\",\"unique_id\":\"" + uniquePrefix +
                        "_lux\",\"state_topic\":\"homeassistant/sensor/" + uniquePrefix +
                        "/lux/state\",\"unit_of_measurement\":\"lx\",\"device_class\":\"illuminance\"," +
                        deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // Motion sensor (binary sensor)
    if (dev->has_motion) {
        String topic = "homeassistant/binary_sensor/" + uniquePrefix + "/motion/config";
        String payload = "{\"name\":\"Motion\",\"unique_id\":\"" + uniquePrefix +
                        "_motion\",\"state_topic\":\"homeassistant/binary_sensor/" + uniquePrefix +
                        "/motion/state\",\"device_class\":\"motion\",\"payload_on\":\"on\",\"payload_off\":\"off\"," +
                        deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // Contact sensor (binary sensor)
    if (dev->has_contact) {
        String sensorType = (dev->contact_type == 1) ? "door" : "window";
        String topic = "homeassistant/binary_sensor/" + uniquePrefix + "/contact/config";
        String payload = "{\"name\":\"Contact\",\"unique_id\":\"" + uniquePrefix +
                        "_contact\",\"state_topic\":\"homeassistant/binary_sensor/" + uniquePrefix +
                        "/contact/state\",\"device_class\":\"" + sensorType +
                        "\",\"payload_on\":\"on\",\"payload_off\":\"off\"," + deviceInfo + "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), MQTT_RETAIN);
    }

    // RSSI diagnostic sensor
    String rssiTopic = "homeassistant/sensor/" + uniquePrefix + "/rssi/config";
    String rssiPayload = "{\"name\":\"RSSI\",\"unique_id\":\"" + uniquePrefix +
                        "_rssi\",\"state_topic\":\"homeassistant/sensor/" + uniquePrefix +
                        "/rssi/state\",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\"," +
                        "\"entity_category\":\"diagnostic\"," + deviceInfo + "}";
    mqttClient.publish(rssiTopic.c_str(), rssiPayload.c_str(), MQTT_RETAIN);

    Serial.printf("[MQTT] Auto-discovery published for %s\n", deviceId);
}

// Publish device sensor data
void publishDeviceData(Device* dev, JsonDocument& doc, int rssi) {
    if (!mqtt_enabled || !mqttClient.connected()) {
        return;
    }

    String gatewayMac = getGatewayMac();
    String uniquePrefix = gatewayMac + "_" + String(dev->id);

    // Publish temperature
    if (doc.containsKey("t") && dev->has_temp) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/temperature/state";
        String value = String(doc["t"].as<float>(), 1);
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish humidity
    if (doc.containsKey("hu") && dev->has_hum) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/humidity/state";
        String value = String(doc["hu"].as<float>(), 0);
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish battery
    if (doc.containsKey("b") && dev->has_batt) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/battery/state";
        String value = String(doc["b"].as<int>());
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish light/lux
    if (doc.containsKey("lux") && dev->has_light) {
        String topic = "homeassistant/sensor/" + uniquePrefix + "/lux/state";
        String value = String(doc["lux"].as<int>());
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish motion (binary sensor)
    if (doc.containsKey("m") && dev->has_motion) {
        String topic = "homeassistant/binary_sensor/" + uniquePrefix + "/motion/state";
        String value = doc["m"].as<bool>() ? "on" : "off";
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish contact (binary sensor)
    if (doc.containsKey("c") && dev->has_contact) {
        String topic = "homeassistant/binary_sensor/" + uniquePrefix + "/contact/state";
        String value = doc["c"].as<bool>() ? "on" : "off";
        mqttClient.publish(topic.c_str(), value.c_str(), MQTT_RETAIN);
    }

    // Publish RSSI
    String rssiTopic = "homeassistant/sensor/" + uniquePrefix + "/rssi/state";
    String rssiValue = String(rssi);
    mqttClient.publish(rssiTopic.c_str(), rssiValue.c_str(), MQTT_RETAIN);
}
