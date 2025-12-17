/*
 * WebServerModule.h - Web Server Handlers
 * All web server routes and handlers for the configuration interface
 */

#ifndef WEBSERVER_MODULE_H
#define WEBSERVER_MODULE_H

#include <WebServer.h>
#include "../core/Config.h"

// ============== Global Objects ==============
extern WebServer webServer;

// ============== Activity Logging ==============
void logActivity(const char* deviceName, const char* message);

// ============== Web Server Functions ==============
void setupWebServer();
void handleRoot();
void handleSave();
void handleReset();
void handleScan();
void handleTestDevice();
void handleUnpair();
void handleRenameDevice();
void handleRemoveDevice();
void handleRestart();
void handleSetSensorType();
void handleHardwareSettings();
void handleNotFound();

#endif // WEBSERVER_MODULE_H
