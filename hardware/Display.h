/*
 * Display.h - OLED Display Functions
 * All display-related functions for the SSD1306 OLED
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <SSD1306Wire.h>
#include "../core/Config.h"

// ============== Global Objects ==============
extern SSD1306Wire display;
extern bool display_available;

// Hardware settings
extern bool oled_enabled;
extern uint8_t oled_brightness;
extern uint16_t oled_timeout;  // seconds, 0 = always on
extern unsigned long oled_last_activity;
extern bool oled_is_off;

// Mode and status flags
extern bool ap_mode;
extern bool wifi_configured;
extern bool homekit_started;
extern char homekit_code_display[10];
extern char homekit_qr_uri[25];

// ============== Display Functions ==============
void displayInit();
void displayMessage(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "");
void displayProgress(const char* title, const char* status, int percent = -1);
void displayPairingScreen();
void displayStatus();
void wakeOled();
void checkOledTimeout();
void feedWatchdog();

#endif // DISPLAY_H
