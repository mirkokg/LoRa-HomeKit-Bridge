/*
 * MQTTModule.h - MQTT Client Module Header
 */

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include "core/Device.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>

// External settings (defined in Settings.cpp)
extern bool mqtt_enabled;
extern char mqtt_server[64];
extern uint16_t mqtt_port;
extern char mqtt_username[32];
extern char mqtt_password[128];
extern char mqtt_topic_prefix[32];
extern uint8_t mqtt_qos;
extern bool mqtt_ssl_enabled;
extern bool mqtt_retain;

// Function declarations
void initMQTT();
void connectMQTT();
void reconnectMQTT();
void loopMQTT();
void disconnectMQTT();
bool isMQTTConnected();
bool testMQTTConnection(const char *server, uint16_t port, const char *username,
                        const char *password);
void publishDeviceData(Device *dev, JsonDocument &doc, int rssi);
void publishHomeAssistantDiscovery(Device *dev, const char *deviceId);
void removeDeviceFromMQTT(const char *deviceId);
void publishBridgeStatus(bool online);
void publishBridgeDiagnostics();

#endif
