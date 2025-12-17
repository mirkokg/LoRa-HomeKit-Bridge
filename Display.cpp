/*
 * Display.cpp - OLED Display Implementation
 */

#include "hardware/Display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HomeSpan.h>
// Use QRCode library wrapper to avoid conflict with ESP32 SDK
#include <QRCode_Library.h>
#include "core/Device.h"

// External global variables
extern float lora_frequency;
extern String last_event;
extern unsigned long last_packet_time;
extern uint32_t packets_received;

// ============== Display Globals ==============
SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);
bool display_available = false;

// OLED hardware settings
bool oled_enabled = true;
uint8_t oled_brightness = 255;
uint16_t oled_timeout = 60;  // seconds
unsigned long oled_last_activity = 0;
bool oled_is_off = false;

void feedWatchdog() {
    // Use vTaskDelay which properly yields to FreeRTOS and feeds watchdog
    vTaskDelay(1);
}

void displayInit() {
    Serial.println("[DISPLAY] Init OLED...");
    Serial.flush();

    // Simply init the display - the library handles I2C internally
    // V1.6/V2.1 has no reset pin, so no manual reset needed
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.clear();
    display.display();
    display_available = true;

    Serial.println("[DISPLAY] OLED ready!");
    Serial.flush();
}

void displayMessage(const char* line1, const char* line2, const char* line3, const char* line4) {
    // Always log to serial
    Serial.printf("[MSG] %s | %s | %s | %s\n", line1, line2, line3, line4);

    if (!display_available || !oled_enabled) return;
    wakeOled();

    display.clear();
    display.setFont(ArialMT_Plain_10);

    if (strlen(line1) > 0) display.drawString(0, 0, line1);
    if (strlen(line2) > 0) display.drawString(0, 16, line2);
    if (strlen(line3) > 0) display.drawString(0, 32, line3);
    if (strlen(line4) > 0) display.drawString(0, 48, line4);

    display.display();
}

void displayProgress(const char* title, const char* status, int percent) {
    Serial.printf("[PROGRESS] %s: %s (%d%%)\n", title, status, percent);

    if (!display_available || !oled_enabled) return;
    wakeOled();

    display.clear();

    // Title
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, title);

    // Status text
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 24, status);

    // Progress bar if percent given
    if (percent >= 0) {
        display.drawRect(0, 50, 128, 10);
        display.fillRect(2, 52, (124 * percent) / 100, 6);
    }

    display.display();
}

void displayPairingScreen() {
    if (!display_available || !oled_enabled) return;
    wakeOled();

    display.clear();

    // Generate QR code from HomeKit URI
    // Version 2 = 25x25 modules, fits nicely on 64px height with scale 2
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(2)];
    qrcode_initText(&qrcode, qrcodeData, 2, ECC_LOW, homekit_qr_uri);

    // QR code size: Version 2 = 25 modules
    int qrSize = qrcode.size;
    int scale = 2;  // 2x2 pixels per module = 50x50 pixels
    int qrPixelSize = qrSize * scale;

    // Position QR code on right side of display, vertically centered
    int qrX = 128 - qrPixelSize - 4;  // Right side with margin
    int qrY = (64 - qrPixelSize) / 2;  // Vertically centered

    // Draw white background for QR code (improves scanning)
    display.setColor(WHITE);
    display.fillRect(qrX - 2, qrY - 2, qrPixelSize + 4, qrPixelSize + 4);

    // Draw QR code modules
    display.setColor(BLACK);
    for (uint8_t y = 0; y < qrSize; y++) {
        for (uint8_t x = 0; x < qrSize; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                display.fillRect(qrX + x * scale, qrY + y * scale, scale, scale);
            }
        }
    }
    display.setColor(WHITE);

    // Draw text on left side
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "Scan to pair");

    // Draw pairing code (split into two lines)
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 14, String(homekit_code_display).substring(0, 4));
    display.drawString(0, 32, String(homekit_code_display).substring(5, 9));

    // Small hint at bottom
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 54, "or Home app");

    display.display();
}

void wakeOled() {
    if (!display_available) return;
    oled_last_activity = millis();
    if (oled_is_off && oled_enabled) {
        display.displayOn();
        oled_is_off = false;
    }
}

void checkOledTimeout() {
    if (!display_available || !oled_enabled || oled_timeout == 0) return;

    if (!oled_is_off && (millis() - oled_last_activity > oled_timeout * 1000)) {
        display.displayOff();
        oled_is_off = true;
    }
}

void displayStatus() {
    if (!display_available || !oled_enabled) return;

    // Check if we should show pairing screen (WiFi connected but not paired)
    if (!ap_mode && homekit_started && WiFi.status() == WL_CONNECTED) {
        bool isPaired = homeSpan.controllerListBegin() != homeSpan.controllerListEnd();
        if (!isPaired) {
            // Show pairing screen with code
            displayPairingScreen();
            return;
        }
    }

    display.clear();
    display.setFont(ArialMT_Plain_10);

    // Header with mode indicator
    String header = ap_mode ? ">>> SETUP MODE <<<" : "LoRa HomeKit Bridge";
    display.drawString(0, 0, header);
    display.drawLine(0, 12, 128, 12);

    if (ap_mode) {
        // AP Mode display
        display.drawString(0, 16, "WiFi: " + String(AP_SSID));
        display.drawString(0, 28, "Pass: " + String(AP_PASSWORD));
        display.drawString(0, 40, "IP: " + WiFi.softAPIP().toString());
        display.drawString(0, 52, "Open browser to setup");
    } else {
        // Normal operation display
        if (WiFi.status() == WL_CONNECTED) {
            display.drawString(0, 16, WiFi.localIP().toString());
        } else {
            display.drawString(0, 16, "WiFi: Reconnecting...");
        }

        display.drawString(0, 28, "LoRa: " + String(lora_frequency, 1) + " MHz");
        display.drawString(0, 40, "Dev:" + String(device_count) + " Pkt:" + String(packets_received));

        // Show last event or pairing code
        if (last_event.length() > 0 && millis() - last_packet_time < 5000) {
            display.drawString(0, 52, last_event.substring(0, 21));
        } else {
            display.drawString(0, 52, "HK: " + String(homekit_code_display));
        }
    }

    display.display();
}
