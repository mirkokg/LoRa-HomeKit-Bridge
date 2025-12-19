/*
 * WiFiModule.h - WiFi Connection and AP Mode
 * WiFi station and access point management
 */

#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include <Arduino.h>
#include <DNSServer.h>
#include "../core/Config.h"

// ============== Global Objects ==============
extern DNSServer dnsServer;

// ============== Mode Flags ==============
extern bool ap_mode;

// ============== WiFi Functions ==============
bool connectWiFi();
void startAPMode();
bool attemptWiFiReconnect();

#endif // WIFI_MODULE_H
