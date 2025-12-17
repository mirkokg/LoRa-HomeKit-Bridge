/*
 * WiFiModule.cpp - WiFi Connection and AP Mode Implementation
 */

#include "network/WiFiModule.h"
#include <WiFi.h>
#include "hardware/Display.h"
#include "data/Settings.h"

// ============== Global Objects ==============
DNSServer dnsServer;

// ============== Mode Flags ==============
bool ap_mode = false;

// ============== WiFi Functions ==============
bool connectWiFi() {
    if (strlen(wifi_ssid) == 0) {
        Serial.println("[WIFI] No SSID configured");
        return false;
    }

    displayProgress("WiFi", ("Connecting to " + String(wifi_ssid)).c_str(), 0);

    Serial.printf("[WIFI] Connecting to: %s\n", wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);

    int timeout = 30; // 15 seconds
    int progress = 0;

    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
        delay(500);
        progress = (30 - timeout) * 100 / 30;
        displayProgress("WiFi", ("Connecting to " + String(wifi_ssid)).c_str(), progress);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        displayProgress("WiFi", ("Connected: " + WiFi.localIP().toString()).c_str(), 100);
        Serial.printf("[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
        delay(1000);
        return true;
    }

    displayMessage("WiFi Failed!", "Could not connect to:", wifi_ssid, "Starting setup mode...");
    Serial.println("[WIFI] Connection failed!");
    delay(2000);
    return false;
}

void startAPMode() {
    displayProgress("Setup Mode", "Starting AP...", 0);

    Serial.println("[AP] Starting Access Point...");
    WiFi.mode(WIFI_AP);
    delay(100);

    WiFi.softAP(AP_SSID, AP_PASSWORD);
    delay(100);

    displayProgress("Setup Mode", "Starting DNS...", 50);

    // Start DNS server for captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    ap_mode = true;

    displayProgress("Setup Mode", "Ready!", 100);
    Serial.printf("[AP] Started: %s / %s\n", AP_SSID, AP_PASSWORD);
    Serial.printf("[AP] IP: %s\n", WiFi.softAPIP().toString().c_str());

    delay(500);
}
