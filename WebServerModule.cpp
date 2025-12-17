/*
 * WebServerModule.cpp - Web Server Implementation
 * All web server handlers and routes for the configuration interface
 */

#include "network/WebServerModule.h"
#include <WiFi.h>
#include <HomeSpan.h>
#include <ArduinoJson.h>
#include "core/Config.h"
#include "core/Device.h"
#include "homekit/DeviceManagement.h"
#include "data/Settings.h"
#include "data/Encryption.h"
#include "hardware/Display.h"
#include "hardware/LoRaModule.h"
#include "network/WiFiModule.h"

// External global variables
extern unsigned long boot_time;
extern uint32_t packets_received;
extern int device_count;

// ============== Activity Log ==============
#define MAX_ACTIVITY_LOG 20

struct ActivityEntry {
    unsigned long timestamp;
    char device_name[32];
    char message[64];
};

ActivityEntry activityLog[MAX_ACTIVITY_LOG];
int activityLogCount = 0;
int activityLogIndex = 0;  // Circular buffer index

void logActivity(const char* deviceName, const char* message) {
    ActivityEntry* entry = &activityLog[activityLogIndex];
    entry->timestamp = millis();
    strncpy(entry->device_name, deviceName, 31);
    entry->device_name[31] = 0;
    strncpy(entry->message, message, 63);
    entry->message[63] = 0;

    activityLogIndex = (activityLogIndex + 1) % MAX_ACTIVITY_LOG;
    if (activityLogCount < MAX_ACTIVITY_LOG) {
        activityLogCount++;
    }
}

// ============== Global Objects ==============
WebServer webServer(80);

// ============== Web Server Handlers ==============
// The web UI is served as a multi-page application with client-side navigation

const char CSS_STYLES[] PROGMEM = R"rawliteral(
:root{--bg-primary:#0a0e14;--bg-secondary:#111821;--bg-tertiary:#1a232f;--bg-card:#151d28;--bg-card-hover:#1a2636;--border-primary:#2a3744;--border-accent:#3d4f5f;--text-primary:#e6edf3;--text-secondary:#8b949e;--text-muted:#6e7681;--accent-primary:#f0883e;--accent-secondary:#db6d28;--accent-glow:rgba(240,136,62,0.3);--success:#3fb950;--success-glow:rgba(63,185,80,0.3);--warning:#d29922;--warning-glow:rgba(210,153,34,0.3);--danger:#f85149;--danger-glow:rgba(248,81,73,0.3);--shadow-md:0 4px 12px rgba(0,0,0,0.5)}
[data-theme="light"]{--bg-primary:#f6f8fa;--bg-secondary:#ffffff;--bg-tertiary:#ebeef1;--bg-card:#ffffff;--bg-card-hover:#f3f6f9;--border-primary:#d0d7de;--border-accent:#a8b3bd;--text-primary:#1f2328;--text-secondary:#656d76;--text-muted:#8b949e;--accent-primary:#d35400;--accent-secondary:#b84700;--accent-glow:rgba(211,84,0,0.15);--success:#1a7f37;--success-glow:rgba(26,127,55,0.15);--warning:#9a6700;--warning-glow:rgba(154,103,0,0.15);--danger:#cf222e;--danger-glow:rgba(207,34,46,0.15);--shadow-md:0 4px 12px rgba(0,0,0,0.1)}
*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg-primary);color:var(--text-primary);min-height:100vh;line-height:1.5;transition:background .3s,color .3s}
.app{display:flex;min-height:100vh;position:relative}.sidebar{width:240px;background:var(--bg-secondary);border-right:1px solid var(--border-primary);display:flex;flex-direction:column;position:fixed;height:100vh;transition:transform .3s;z-index:100}
.sidebar-header{padding:16px;border-bottom:1px solid var(--border-primary)}.logo{display:flex;align-items:center;gap:10px}.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));border-radius:8px;display:flex;align-items:center;justify-content:center}
.logo-icon svg{width:20px;height:20px;fill:#fff}.logo-text{display:flex;flex-direction:column}.logo-title{font-size:14px;font-weight:700}.logo-subtitle{font-size:9px;color:var(--text-secondary);text-transform:uppercase;letter-spacing:1px}
.conn-status{display:flex;align-items:center;gap:8px;padding:8px 12px;margin:12px;background:var(--bg-tertiary);border-radius:6px;border:1px solid var(--border-primary)}.status-led{width:6px;height:6px;border-radius:50%;background:var(--success);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}.status-text{font-size:11px;color:var(--text-secondary)}.nav-section{padding:4px 12px}.nav-label{font-size:9px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px;padding:8px 6px 4px}
.nav-item{display:flex;align-items:center;gap:8px;padding:8px 12px;border-radius:6px;color:var(--text-secondary);font-size:12px;font-weight:500;cursor:pointer;border:1px solid transparent;margin-bottom:2px;transition:all .2s;text-decoration:none}
.nav-item:hover{background:var(--bg-tertiary);color:var(--text-primary)}.nav-item.active{background:var(--accent-glow);color:var(--accent-primary);border-color:var(--accent-primary)}.nav-item svg{width:16px;height:16px;flex-shrink:0}
.sidebar-footer{margin-top:auto;padding:12px;border-top:1px solid var(--border-primary)}.theme-toggle{display:flex;align-items:center;justify-content:space-between;padding:8px 12px;background:var(--bg-tertiary);border-radius:6px;border:1px solid var(--border-primary)}
.theme-label{font-size:11px;color:var(--text-secondary);display:flex;align-items:center;gap:6px}.theme-label svg{width:14px;height:14px}.toggle-sw{width:40px;height:22px;background:var(--bg-card);border-radius:11px;cursor:pointer;position:relative;border:2px solid var(--border-primary);transition:all .3s}
.toggle-sw::after{content:'';position:absolute;width:14px;height:14px;background:var(--accent-primary);border-radius:50%;top:2px;left:2px;transition:transform .3s}[data-theme="dark"] .toggle-sw::after{transform:translateX(18px)}
.main{flex:1;margin-left:240px;padding:20px;min-height:100vh}.page{display:none;animation:fadeIn .3s}.page.active{display:block}@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}
.page-header{margin-bottom:20px}.page-title{font-size:20px;font-weight:700;margin-bottom:4px}.page-desc{color:var(--text-secondary);font-size:13px}
.card{background:var(--bg-card);border:1px solid var(--border-primary);border-radius:10px;padding:16px;margin-bottom:16px;transition:all .3s}.card:hover{border-color:var(--border-accent)}
.card-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;padding-bottom:12px;border-bottom:1px solid var(--border-primary)}.card-title{font-size:14px;font-weight:600;display:flex;align-items:center;gap:6px}.card-title svg{width:16px;height:16px;color:var(--accent-primary)}
.grid-2{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
.status-item{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;padding:12px;display:flex;flex-direction:column;gap:4px}.status-label{font-size:9px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px}.status-value{font-family:monospace;font-size:13px;font-weight:600;color:var(--text-primary)}.status-value.hl{color:var(--accent-primary)}
.badge{display:inline-flex;align-items:center;gap:4px;padding:2px 8px;border-radius:12px;font-size:10px;font-weight:600}.badge.success{background:var(--success-glow);color:var(--success)}.badge.warning{background:var(--warning-glow);color:var(--warning)}.badge.danger{background:var(--danger-glow);color:var(--danger)}.badge::before{content:'';width:4px;height:4px;border-radius:50%;background:currentColor}
.form-group{margin-bottom:14px}.form-label{display:block;font-size:11px;font-weight:600;color:var(--text-secondary);margin-bottom:4px}.form-input,.form-select{width:100%;padding:8px 12px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;color:var(--text-primary);font-family:monospace;font-size:12px;transition:all .2s}
.form-input:focus,.form-select:focus{outline:none;border-color:var(--accent-primary)}.form-hint{font-size:10px;color:var(--text-muted);margin-top:3px}.form-hint.warning{color:var(--warning);display:flex;align-items:center;gap:4px;padding:8px;background:var(--warning-glow);border-radius:6px;margin-bottom:14px}
.toggle-group{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;margin-bottom:8px}.toggle-info{display:flex;flex-direction:column;gap:2px}.toggle-title{font-size:12px;font-weight:600;color:var(--text-primary)}.toggle-desc{font-size:10px;color:var(--text-muted)}
.toggle-btn{width:44px;height:24px;background:var(--bg-primary);border-radius:12px;cursor:pointer;position:relative;border:2px solid var(--border-primary);transition:all .3s;flex-shrink:0}.toggle-btn::after{content:'';position:absolute;width:16px;height:16px;background:var(--text-muted);border-radius:50%;top:2px;left:2px;transition:all .3s}.toggle-btn.active{background:var(--accent-glow);border-color:var(--accent-primary)}.toggle-btn.active::after{background:var(--accent-primary);transform:translateX(20px)}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:8px 16px;border-radius:6px;font-size:12px;font-weight:600;cursor:pointer;transition:all .2s;border:none}.btn svg{width:14px;height:14px}.btn-primary{background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));color:#fff}.btn-primary:hover{transform:translateY(-1px)}.btn-secondary{background:var(--bg-tertiary);color:var(--text-primary);border:1px solid var(--border-primary)}.btn-secondary:hover{background:var(--bg-card-hover)}.btn-danger{background:var(--danger);color:#fff}.btn-danger:hover{transform:translateY(-1px)}.btn-warning{background:var(--warning);color:#fff}.btn-group{display:flex;gap:8px;flex-wrap:wrap}
.qr-container{display:flex;flex-direction:column;align-items:center;padding:20px;background:var(--bg-tertiary);border-radius:10px;border:1px solid var(--border-primary)}.qr-code{width:160px;height:160px;background:#fff;border-radius:10px;padding:10px;margin-bottom:14px}.qr-code img{width:100%;height:100%;image-rendering:pixelated}.hk-code{font-family:monospace;font-size:22px;font-weight:700;letter-spacing:2px;color:var(--text-primary);margin-bottom:4px}.hk-code-label{font-size:10px;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px}
.device-card{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;padding:12px;display:flex;align-items:center;gap:12px;margin-bottom:8px;transition:all .2s}.device-card:hover{border-color:var(--accent-primary)}.device-icon{width:40px;height:40px;background:var(--bg-card);border-radius:8px;display:flex;align-items:center;justify-content:center;border:1px solid var(--border-primary)}.device-icon svg{width:20px;height:20px;color:var(--accent-primary)}.device-info{flex:1}.device-name{font-weight:600;font-size:13px;margin-bottom:2px}.device-meta{font-size:10px;color:var(--text-muted);font-family:monospace}.device-signal{display:flex;gap:2px;align-items:flex-end;height:20px;margin-right:8px}.signal-bar{width:4px;background:var(--border-primary);border-radius:2px;transition:all .3s}.signal-bar:nth-child(1){height:6px}.signal-bar:nth-child(2){height:10px}.signal-bar:nth-child(3){height:14px}.signal-bar:nth-child(4){height:18px}.signal-bar.active{background:var(--accent-primary)}.device-actions{display:flex;gap:4px}.device-btn{padding:4px 8px;font-size:10px;border-radius:4px;cursor:pointer;background:var(--bg-card);border:1px solid var(--border-primary);color:var(--text-secondary);transition:all .2s}.device-btn:hover{border-color:var(--accent-primary);color:var(--accent-primary)}.device-btn.danger:hover{border-color:var(--danger);color:var(--danger)}
.activity-entry{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;padding:8px 10px;margin-bottom:6px;font-size:11px;display:flex;gap:8px;align-items:flex-start}.activity-time{color:var(--text-muted);font-family:monospace;white-space:nowrap;font-size:10px}.activity-device{color:var(--accent-primary);font-weight:600;white-space:nowrap;min-width:80px}.activity-msg{color:var(--text-secondary);font-family:monospace;flex:1;word-break:break-all;font-size:10px}
.test-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}.test-btn{padding:14px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;display:flex;flex-direction:column;align-items:center;gap:8px;cursor:pointer;transition:all .2s;color:var(--text-primary)}.test-btn:hover{border-color:var(--accent-primary);background:var(--bg-card-hover);transform:translateY(-1px)}.test-btn svg{width:20px;height:20px;color:var(--accent-primary)}.test-btn span{font-size:11px;font-weight:600}
.action-card{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;padding:14px;display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}.action-info{display:flex;align-items:center;gap:12px}.action-icon{width:36px;height:36px;background:var(--bg-card);border-radius:8px;display:flex;align-items:center;justify-content:center;border:1px solid var(--border-primary)}.action-icon svg{width:18px;height:18px}.action-icon.warning svg{color:var(--warning)}.action-icon.danger svg{color:var(--danger)}.action-text h4{font-size:13px;font-weight:600;margin-bottom:2px}.action-text p{font-size:11px;color:var(--text-muted)}
.mobile-menu{display:none;position:fixed;top:12px;left:12px;z-index:200;width:36px;height:36px;background:var(--bg-secondary);border:1px solid var(--border-primary);border-radius:8px;cursor:pointer;align-items:center;justify-content:center}.mobile-menu svg{width:20px;height:20px;color:var(--text-primary)}.sidebar-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.5);z-index:99}
@media(max-width:900px){.grid-2{grid-template-columns:1fr}}@media(max-width:768px){.sidebar{transform:translateX(-100%)}.sidebar.open{transform:translateX(0)}.sidebar-overlay.active{display:block}.mobile-menu{display:flex}.main{margin-left:0;padding:60px 12px 16px}.page-title{font-size:18px}.status-grid{grid-template-columns:1fr}.qr-code{width:140px;height:140px}.hk-code{font-size:18px}}
)rawliteral";

void handleRoot() {
    String html;
    html.reserve(32000);
    
    bool isPaired = homekit_started && (homeSpan.controllerListBegin() != homeSpan.controllerListEnd());
    int activeDevices = getActiveDeviceCount();
    unsigned long uptime = (millis() - boot_time) / 1000;
    String uptimeStr = (uptime >= 3600) ? String(uptime/3600)+"h "+String((uptime%3600)/60)+"m" : String(uptime/60)+"m "+String(uptime%60)+"s";
    
    String encKeyHex = "";
    for (int i = 0; i < encrypt_key_len; i++) { char hex[3]; sprintf(hex, "%02X", encrypt_key[i]); encKeyHex += hex; }
    char syncHex[5]; sprintf(syncHex, "%02X", lora_syncword);
    
    html += F("<!DOCTYPE html><html lang=\"en\" data-theme=\"light\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LoRa HomeKit Bridge</title><style>");
    html += FPSTR(CSS_STYLES);
    html += F("</style></head><body>");
    
    // Mobile menu and overlay
    html += F("<div class=\"sidebar-overlay\" onclick=\"toggleSidebar()\"></div>");
    html += F("<button class=\"mobile-menu\" onclick=\"toggleSidebar()\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M3 12h18M3 6h18M3 18h18\"/></svg></button>");
    
    // App container
    html += F("<div class=\"app\">");
    
    // Sidebar
    html += F("<aside class=\"sidebar\" id=\"sidebar\"><div class=\"sidebar-header\"><div class=\"logo\"><div class=\"logo-icon\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2L2 7v10l10 5 10-5V7L12 2z\"/></svg></div><div class=\"logo-text\"><span class=\"logo-title\">LoRa HomeKit</span><span class=\"logo-subtitle\">Control Panel</span></div></div></div>");
    html += F("<div class=\"conn-status\"><div class=\"status-led\"></div><span class=\"status-text\">");
    html += ap_mode ? "Setup Mode" : "Connected";
    html += F("</span></div>");
    
    // Navigation
    html += F("<nav class=\"nav-section\"><div class=\"nav-label\">Main</div>");
    html += F("<a class=\"nav-item active\" data-page=\"status\" href=\"#/status\" onclick=\"navigateTo('status');return false;\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M3 9h18M9 21V9\"/></svg>Status</a>");
    html += F("<a class=\"nav-item\" data-page=\"homekit\" href=\"#/homekit\" onclick=\"navigateTo('homekit');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\"/><path d=\"M9 22V12h6v10\"/></svg>HomeKit</a>");
    html += F("<a class=\"nav-item\" data-page=\"devices\" href=\"#/devices\" onclick=\"navigateTo('devices');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"20\" height=\"14\" x=\"2\" y=\"3\" rx=\"2\"/><path d=\"M8 21h8m-4-4v4\"/></svg>Devices</a>");
    html += F("<a class=\"nav-item\" data-page=\"test\" href=\"#/test\" onclick=\"navigateTo('test');return false;\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z\"/></svg>Test</a>");
    html += F("</nav><nav class=\"nav-section\"><div class=\"nav-label\">Settings</div>");
    html += F("<a class=\"nav-item\" data-page=\"wifi\" href=\"#/wifi\" onclick=\"navigateTo('wifi');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0\"/><circle cx=\"12\" cy=\"20\" r=\"1\"/></svg>WiFi</a>");
    html += F("<a class=\"nav-item\" data-page=\"lora\" href=\"#/lora\" onclick=\"navigateTo('lora');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M4.9 19.1C1 15.2 1 8.8 4.9 4.9m2.9 11.3c-2.3-2.3-2.3-6.1 0-8.5\"/><circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5m2.9-11.4C23 8.8 23 15.1 19.1 19\"/></svg>LoRa</a>");
    html += F("<a class=\"nav-item\" data-page=\"encryption\" href=\"#/encryption\" onclick=\"navigateTo('encryption');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"18\" height=\"11\" x=\"3\" y=\"11\" rx=\"2\"/><path d=\"M7 11V7a5 5 0 0 1 10 0v4\"/><circle cx=\"12\" cy=\"16\" r=\"1\"/></svg>Encryption</a>");
    html += F("<a class=\"nav-item\" data-page=\"hardware\" href=\"#/hardware\" onclick=\"navigateTo('hardware');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"16\" height=\"16\" x=\"4\" y=\"4\" rx=\"2\"/><path d=\"M9 9h6v6H9zm0-7v2m6-2v2M9 20v2m6-2v2M2 9h2m-2 6h2m16-6h2m-2 6h2\"/></svg>Hardware</a>");
    html += F("</nav><nav class=\"nav-section\"><div class=\"nav-label\">Actions</div>");
    html += F("<a class=\"nav-item\" data-page=\"actions\" href=\"#/actions\" onclick=\"navigateTo('actions');return false;\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M23 4v6h-6M1 20v-6h6\"/><path d=\"M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15\"/></svg>System</a>");
    html += F("</nav><div class=\"sidebar-footer\"><div class=\"theme-toggle\"><span class=\"theme-label\"><svg viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M12 22c5.523 0 10-4.477 10-10S17.523 2 12 2 2 6.477 2 12s4.477 10 10 10Zm0-1.5v-17a8.5 8.5 0 1 1 0 17Z\"/></svg>Dark Mode</span><div class=\"toggle-sw\" onclick=\"toggleTheme()\"></div></div></div></aside>");
    
    // Main content
    html += F("<main class=\"main\">");

    // Status Page
    html += F("<div class=\"page active\" id=\"page-status\"><div class=\"page-header\"><h1 class=\"page-title\">System Status</h1><p class=\"page-desc\">Overview of your LoRa HomeKit Bridge</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M22 11.08V12a10 10 0 1 1-5.93-9.14\"/><path d=\"M22 4 12 14.01l-3-3\"/></svg>Connection</h3>");
    html += ap_mode ? F("<span class=\"badge warning\">Setup Mode</span>") : F("<span class=\"badge success\">Online</span>");
    html += F("</div><div class=\"status-grid\">");
    if (ap_mode) {
        html += F("<div class=\"status-item\"><span class=\"status-label\">AP Name</span><span class=\"status-value\">"); html += AP_SSID; html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Password</span><span class=\"status-value\">"); html += AP_PASSWORD; html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">IP Address</span><span class=\"status-value\">"); html += WiFi.softAPIP().toString(); html += F("</span></div>");
    } else {
        html += F("<div class=\"status-item\"><span class=\"status-label\">WiFi</span><span class=\"status-value hl\">Connected</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">IP Address</span><span class=\"status-value\">"); html += WiFi.localIP().toString(); html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Signal</span><span class=\"status-value\">"); html += String(WiFi.RSSI()); html += F(" dBm</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Network</span><span class=\"status-value\">"); html += wifi_ssid; html += F("</span></div>");
    }
    html += F("</div></div><div class=\"grid-2\"><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M4.9 19.1C1 15.2 1 8.8 4.9 4.9m2.9 11.3c-2.3-2.3-2.3-6.1 0-8.5\"/><circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5m2.9-11.4C23 8.8 23 15.1 19.1 19\"/></svg>LoRa Radio</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Frequency</span><span class=\"status-value\">"); html += String(lora_frequency,1); html += F(" MHz</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">SF</span><span class=\"status-value\">SF"); html += String(lora_sf); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Bandwidth</span><span class=\"status-value\">"); html += String(lora_bw/1000); html += F(" kHz</span></div>");
    html += F("</div></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M3 3v18h18m-3-4V9m-5 8V5M8 17v-3\"/></svg>Statistics</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Devices</span><span class=\"status-value hl\">"); html += String(activeDevices); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Packets</span><span class=\"status-value\">"); html += String(packets_received); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Uptime</span><span class=\"status-value\">"); html += uptimeStr; html += F("</span></div>");
    html += F("</div></div></div></div>");
    
    // HomeKit Page
    html += F("<div class=\"page\" id=\"page-homekit\"><div class=\"page-header\"><h1 class=\"page-title\">HomeKit Pairing</h1><p class=\"page-desc\">Pair with Apple HomeKit</p></div><div class=\"grid-2\"><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">QR Code</h3></div><div class=\"qr-container\"><div class=\"qr-code\" id=\"qrcode\"></div><div class=\"hk-code\">");
    html += homekit_code_display;
    html += F("</div><div class=\"hk-code-label\">Setup Code</div></div></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Pairing Status</h3>");
    html += isPaired ? F("<span class=\"badge success\">Paired</span>") : F("<span class=\"badge warning\">Not Paired</span>");
    html += F("</div><div class=\"status-grid\"><div class=\"status-item\"><span class=\"status-label\">Status</span><span class=\"status-value hl\">"); 
    html += isPaired ? "Paired" : "Waiting"; 
    html += F("</span></div><div class=\"status-item\"><span class=\"status-label\">Accessories</span><span class=\"status-value\">"); html += String(activeDevices); html += F("</span></div></div>");
    if (isPaired) { html += F("<div style=\"margin-top:14px\"><button class=\"btn btn-danger\" onclick=\"unpairHomeKit()\">Unpair HomeKit</button></div>"); }
    html += F("</div></div></div>");
    
    // Devices Page
    html += F("<div class=\"page\" id=\"page-devices\"><div class=\"page-header\"><h1 class=\"page-title\">Devices</h1><p class=\"page-description\">Manage your connected LoRa devices</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"2\" y=\"3\" width=\"20\" height=\"14\" rx=\"2\"></rect><line x1=\"8\" y1=\"21\" x2=\"16\" y2=\"21\"></line><line x1=\"12\" y1=\"17\" x2=\"12\" y2=\"21\"></line></svg>Connected Devices ("); html += String(activeDevices); html += F(")</h3><button class=\"btn btn-secondary\" onclick=\"location.reload()\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M23 4v6h-6M1 20v-6h6\"></path><path d=\"M3.51 9a9 9 0 0114.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0020.49 15\"></path></svg>Refresh</button></div>");
    if (activeDevices == 0) {
        html += F("<p style=\"color:var(--text-muted);font-size:14px\">No devices yet. Add test devices or wait for LoRa sensors.</p>");
    } else {
        for (int i = 0; i < device_count; i++) {
            if (!devices[i].active) continue;

            // Debug logging
            Serial.printf("[WEB] Device %s: has_contact=%d, has_motion=%d, contact_type=%d, motion_type=%d\n",
                         devices[i].id, devices[i].has_contact, devices[i].has_motion,
                         devices[i].contact_type, devices[i].motion_type);

            // Determine device type label
            String deviceType = "Sensor";
            if (devices[i].has_motion) {
                deviceType = "Motion Sensor";
            } else if (devices[i].has_contact) {
                deviceType = "Contact Sensor";
            } else if (devices[i].has_temp && devices[i].has_hum) {
                deviceType = "Climate Sensor";
            } else if (devices[i].has_temp) {
                deviceType = "Temperature Sensor";
            } else if (devices[i].has_hum) {
                deviceType = "Humidity Sensor";
            } else if (devices[i].has_light) {
                deviceType = "Light Sensor";
            }

            // Calculate signal bars based on RSSI
            int signalBars = 4;
            if (devices[i].rssi < -80) signalBars = 1;
            else if (devices[i].rssi < -70) signalBars = 2;
            else if (devices[i].rssi < -60) signalBars = 3;

            html += F("<div class=\"device-card\"><div class=\"device-icon\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"2\"></rect><circle cx=\"12\" cy=\"12\" r=\"3\"></circle></svg></div><div class=\"device-info\"><div class=\"device-name\">");
            html += devices[i].name;
            html += F("</div><div class=\"device-meta\">");
            html += deviceType;
            html += " • RSSI: " + String(devices[i].rssi) + "dBm";
            if (devices[i].has_batt) {
                html += " • " + String((int)devices[i].battery) + "%";
            }
            html += F("</div>");

            // Add sensor type selector for motion/contact sensors
            if (devices[i].has_contact) {
                html += F("<div style=\"margin-top:6px;font-size:10px\"><label style=\"color:var(--text-muted)\">Type: </label><select class=\"form-select\" style=\"display:inline-block;width:auto;padding:2px 6px;font-size:10px\" onchange=\"setSensorType('");
                html += devices[i].id;
                html += F("','contact',this.value)\"><option value=\"0\"");
                if (devices[i].contact_type == CONTACT_TYPE_CONTACT) html += " selected";
                html += F(">Contact</option><option value=\"1\"");
                if (devices[i].contact_type == CONTACT_TYPE_LEAK) html += " selected";
                html += F(">⚡ Leak</option><option value=\"2\"");
                if (devices[i].contact_type == CONTACT_TYPE_SMOKE) html += " selected";
                html += F(">⚡ Smoke</option><option value=\"3\"");
                if (devices[i].contact_type == CONTACT_TYPE_CO) html += " selected";
                html += F(">⚡ CO</option><option value=\"4\"");
                if (devices[i].contact_type == CONTACT_TYPE_OCCUPANCY) html += " selected";
                html += F(">Occupancy</option></select></div>");
            }

            if (devices[i].has_motion) {
                html += F("<div style=\"margin-top:6px;font-size:10px\"><label style=\"color:var(--text-muted)\">Type: </label><select class=\"form-select\" style=\"display:inline-block;width:auto;padding:2px 6px;font-size:10px\" onchange=\"setSensorType('");
                html += devices[i].id;
                html += F("','motion',this.value)\"><option value=\"0\"");
                if (devices[i].motion_type == MOTION_TYPE_MOTION) html += " selected";
                html += F(">Motion</option><option value=\"1\"");
                if (devices[i].motion_type == MOTION_TYPE_OCCUPANCY) html += " selected";
                html += F(">Occupancy</option><option value=\"2\"");
                if (devices[i].motion_type == MOTION_TYPE_LEAK) html += " selected";
                html += F(">⚡ Leak</option><option value=\"3\"");
                if (devices[i].motion_type == MOTION_TYPE_SMOKE) html += " selected";
                html += F(">⚡ Smoke</option><option value=\"4\"");
                if (devices[i].motion_type == MOTION_TYPE_CO) html += " selected";
                html += F(">⚡ CO</option></select></div>");
            }

            html += F("</div><div class=\"device-signal\">");
            for (int bar = 1; bar <= 4; bar++) {
                html += F("<div class=\"signal-bar");
                if (bar <= signalBars) html += F(" active");
                html += F("\"></div>");
            }
            html += F("</div><div class=\"device-actions\"><button class=\"device-btn\" onclick=\"renameDevice('"); html += devices[i].id; html += F("','"); html += devices[i].name; html += F("')\">Rename</button><button class=\"device-btn danger\" onclick=\"removeDevice('"); html += devices[i].id; html += F("')\">Remove</button></div></div>");
        }
    }
    html += F("</div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"12\" cy=\"12\" r=\"10\"></circle><path d=\"M12 6v6l4 2\"></path></svg>Device Activity</h3></div>");

    // Display activity log entries (reverse chronological order)
    if (activityLogCount == 0) {
        html += F("<p style=\"color: var(--text-muted); font-size: 14px;\">No recent activity. Waiting for device messages...</p>");
    } else {
        // Show entries in reverse order (newest first)
        int displayCount = min(activityLogCount, 10);  // Show last 10 entries
        for (int i = 0; i < displayCount; i++) {
            // Calculate index for reverse chronological order
            int idx = (activityLogIndex - 1 - i + MAX_ACTIVITY_LOG) % MAX_ACTIVITY_LOG;
            if (idx < 0) idx += MAX_ACTIVITY_LOG;

            ActivityEntry* entry = &activityLog[idx];

            // Calculate time ago
            unsigned long secondsAgo = (millis() - entry->timestamp) / 1000;
            String timeStr;
            if (secondsAgo < 60) {
                timeStr = String(secondsAgo) + "s ago";
            } else if (secondsAgo < 3600) {
                timeStr = String(secondsAgo / 60) + "m ago";
            } else {
                timeStr = String(secondsAgo / 3600) + "h ago";
            }

            html += F("<div class=\"activity-entry\"><span class=\"activity-time\">");
            html += timeStr;
            html += F("</span><span class=\"activity-device\">");
            html += entry->device_name;
            html += F("</span><span class=\"activity-msg\">");
            html += entry->message;
            html += F("</span></div>");
        }
    }

    html += F("</div></div>");
    
    // Test Page
    html += F("<div class=\"page\" id=\"page-test\"><div class=\"page-header\"><h1 class=\"page-title\">Test Sensors</h1><p class=\"page-desc\">Add simulated sensors</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Simulated Sensors</h3></div><div class=\"test-grid\">");
    html += F("<button class=\"test-btn\" onclick=\"addTest('temp')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14 14.76V3.5a2.5 2.5 0 00-5 0v11.26a4.5 4.5 0 105 0z\"/></svg><span>Temperature</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('humidity')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M12 2.69l5.66 5.66a8 8 0 11-11.31 0z\"/></svg><span>Humidity</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('temp_hum')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14 14.76V3.5a2.5 2.5 0 00-5 0v11.26\"/></svg><span>Temp+Hum</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('motion')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg><span>Motion</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('contact')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/></svg><span>Contact</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('light')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"12\" cy=\"12\" r=\"5\"/></svg><span>Light</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('full')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"16\" height=\"16\" x=\"4\" y=\"4\" rx=\"2\"/><path d=\"M9 9h6v6H9z\"/></svg><span>Full Sensor</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('update')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M23 4v6h-6\"/></svg><span>Update Values</span></button>");
    html += F("</div><p id=\"test-status\" style=\"color:var(--accent-primary);font-size:11px;margin-top:10px;text-align:center\"></p></div></div>");
    
    // WiFi Page
    html += F("<div class=\"page\" id=\"page-wifi\"><div class=\"page-header\"><h1 class=\"page-title\">WiFi Settings</h1><p class=\"page-desc\">Configure network</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Network</h3></div>");
    html += F("<form id=\"wifiForm\" onsubmit=\"return saveSettings(event)\"><div class=\"form-group\"><label class=\"form-label\">WiFi Network</label><div style=\"display:flex;gap:8px\"><select class=\"form-select\" id=\"wifiSelect\" style=\"flex:1\" onchange=\"document.getElementById('ssid').value=this.value\"><option value=\"\">-- Scan --</option></select><button type=\"button\" class=\"btn btn-secondary\" onclick=\"scanWifi()\">Scan</button></div></div>");
    html += F("<div class=\"form-group\"><label class=\"form-label\">SSID</label><input type=\"text\" class=\"form-input\" id=\"ssid\" name=\"ssid\" value=\""); html += wifi_ssid;
    html += F("\"></div><div class=\"form-group\"><label class=\"form-label\">Password</label><input type=\"password\" class=\"form-input\" name=\"password\" placeholder=\"Leave empty to keep current\"></div>");
    html += F("<button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // LoRa Page
    html += F("<div class=\"page\" id=\"page-lora\"><div class=\"page-header\"><h1 class=\"page-title\">LoRa Settings</h1><p class=\"page-desc\">Configure radio</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Radio Configuration</h3></div>");
    html += F("<p class=\"form-hint warning\"><svg width=\"12\" height=\"12\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z\"/></svg>Must match your sensors!</p>");
    html += F("<form id=\"loraForm\" onsubmit=\"return saveSettings(event)\"><div class=\"grid-2\"><div class=\"form-group\"><label class=\"form-label\">Frequency</label><select class=\"form-select\" name=\"freq\"><option value=\"433.0\"");
    if (lora_frequency < 500) html += " selected";
    html += F(">433 MHz</option><option value=\"868.0\"");
    if (lora_frequency > 800 && lora_frequency < 900) html += " selected";
    html += F(">868 MHz</option><option value=\"915.0\"");
    if (lora_frequency > 900) html += " selected";
    html += F(">915 MHz</option></select><p class=\"form-hint\">Select based on your region's regulations</p></div><div class=\"form-group\"><label class=\"form-label\">Spreading Factor</label><select class=\"form-select\" name=\"lora_sf\">");
    for (int sf = 6; sf <= 12; sf++) { html += "<option value=\"" + String(sf) + "\""; if (lora_sf == sf) html += " selected"; html += ">SF" + String(sf) + "</option>"; }
    html += F("</select><p class=\"form-hint\">Higher SF = longer range, lower data rate</p></div><div class=\"form-group\"><label class=\"form-label\">Bandwidth</label><select class=\"form-select\" name=\"lora_bw\"><option value=\"125000\"");
    if (lora_bw == 125000) html += " selected";
    html += F(">125 kHz</option><option value=\"250000\"");
    if (lora_bw == 250000) html += " selected";
    html += F(">250 kHz</option><option value=\"500000\"");
    if (lora_bw == 500000) html += " selected";
    html += F(">500 kHz</option></select><p class=\"form-hint\">Wider = faster data, narrower = better range</p></div><div class=\"form-group\"><label class=\"form-label\">Coding Rate</label><select class=\"form-select\" name=\"lora_cr\"><option value=\"5\"");
    if (lora_cr == 5) html += " selected";
    html += F(">4/5</option><option value=\"6\"");
    if (lora_cr == 6) html += " selected";
    html += F(">4/6</option><option value=\"7\"");
    if (lora_cr == 7) html += " selected";
    html += F(">4/7</option><option value=\"8\"");
    if (lora_cr == 8) html += " selected";
    html += F(">4/8</option></select><p class=\"form-hint\">Higher values add error correction at slower speeds</p></div><div class=\"form-group\"><label class=\"form-label\">Preamble</label><input type=\"number\" class=\"form-input\" name=\"lora_pre\" value=\""); html += String(lora_preamble);
    html += F("\" min=\"6\" max=\"65535\"><p class=\"form-hint\">Longer preambles improve sync but increase airtime</p></div><div class=\"form-group\"><label class=\"form-label\">Sync Word</label><input type=\"text\" class=\"form-input\" name=\"lora_sync\" value=\""); html += syncHex;
    html += F("\" maxlength=\"2\"><p class=\"form-hint\">Network identifier - must match all devices</p></div></div><button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // Encryption Page
    html += F("<div class=\"page\" id=\"page-encryption\"><div class=\"page-header\"><h1 class=\"page-title\">Encryption</h1><p class=\"page-desc\">Configure data encryption</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Encryption</h3></div>");
    html += F("<p class=\"form-hint warning\"><svg width=\"12\" height=\"12\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z\"/></svg>Must match your sensors!</p>");
    html += F("<form id=\"encForm\" onsubmit=\"return saveSettings(event)\"><div class=\"form-group\"><label class=\"form-label\">Gateway Key</label><input type=\"text\" class=\"form-input\" name=\"gw_key\" value=\""); html += gateway_key;
    html += F("\"><p class=\"form-hint\">Sensors with different keys ignored</p></div><div class=\"form-group\"><label class=\"form-label\">Mode</label><select class=\"form-select\" name=\"enc_mode\"><option value=\"0\"");
    if (encryption_mode == 0) html += " selected";
    html += F(">None</option><option value=\"1\"");
    if (encryption_mode == 1) html += " selected";
    html += F(">XOR</option><option value=\"2\"");
    if (encryption_mode == 2) html += " selected";
    html += F(">AES-128</option></select></div><div class=\"form-group\"><label class=\"form-label\">Key (hex)</label><input type=\"text\" class=\"form-input\" name=\"enc_key\" value=\""); html += encKeyHex;
    html += F("\"><p class=\"form-hint\">XOR: 2-32 chars | AES: 32 chars</p></div><button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // Hardware Page  
    html += F("<div class=\"page\" id=\"page-hardware\"><div class=\"page-header\"><h1 class=\"page-title\">Hardware</h1><p class=\"page-desc\">Configure LEDs and display</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">LED Indicators</h3></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">Power LED</span><span class=\"toggle-desc\">Shows when powered</span></div><div class=\"toggle-btn"); if (power_led_enabled) html += " active"; html += F("\" id=\"pwrLed\" onclick=\"toggleHw('pwr_led')\"></div></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">Activity LED</span><span class=\"toggle-desc\">Blinks on packets</span></div><div class=\"toggle-btn"); if (activity_led_enabled) html += " active"; html += F("\" id=\"actLed\" onclick=\"toggleHw('act_led')\"></div></div></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Display</h3></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">OLED Screen</span><span class=\"toggle-desc\">Enable display</span></div><div class=\"toggle-btn"); if (oled_enabled) html += " active"; html += F("\" id=\"oledEn\" onclick=\"toggleHw('oled_en')\"></div></div>");
    html += F("<div class=\"form-group\" style=\"margin-top:12px\"><label class=\"form-label\">Screen Timeout</label><select class=\"form-select\" id=\"oledTimeout\" onchange=\"setHwVal('oled_to',this.value)\"><option value=\"0\""); if (oled_timeout == 0) html += " selected"; html += F(">Never</option><option value=\"30\""); if (oled_timeout == 30) html += " selected"; html += F(">30s</option><option value=\"60\""); if (oled_timeout == 60) html += " selected"; html += F(">1 min</option><option value=\"300\""); if (oled_timeout == 300) html += " selected"; html += F(">5 min</option></select></div>");
    html += F("<div class=\"form-group\"><label class=\"form-label\">Brightness</label><input type=\"range\" id=\"oledBr\" min=\"1\" max=\"255\" value=\""); html += String(oled_brightness); html += F("\" style=\"width:100%;accent-color:var(--accent-primary)\" onchange=\"setHwVal('oled_br',this.value)\"></div></div></div>");
    
    // Actions Page
    html += F("<div class=\"page\" id=\"page-actions\"><div class=\"page-header\"><h1 class=\"page-title\">System</h1><p class=\"page-desc\">Device management</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Maintenance</h3></div>");
    html += F("<div class=\"action-card\"><div class=\"action-info\"><div class=\"action-icon warning\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M23 4v6h-6M1 20v-6h6\"/><path d=\"M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15\"/></svg></div><div class=\"action-text\"><h4>Restart</h4><p>Reboot device</p></div></div><button class=\"btn btn-warning\" onclick=\"restartDevice()\">Restart</button></div>");
    html += F("<div class=\"action-card\"><div class=\"action-info\"><div class=\"action-icon danger\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M3 6h18m-2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2m-6 5v6m4-6v6\"/></svg></div><div class=\"action-text\"><h4>Factory Reset</h4><p>Erase all settings</p></div></div><button class=\"btn btn-danger\" onclick=\"factoryReset()\">Reset</button></div></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Info</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Firmware</span><span class=\"status-value\">v2.0</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Hardware</span><span class=\"status-value\">TTGO LoRa32</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">MAC</span><span class=\"status-value\">"); html += WiFi.macAddress(); html += F("</span></div>");
    html += F("</div></div></div>");
    
    html += F("</main></div>");
    
    // JavaScript
    html += F("<script src=\"https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.min.js\"></script><script>");
    html += F("function showPage(p){document.querySelectorAll('.page').forEach(e=>e.classList.remove('active'));document.getElementById('page-'+p).classList.add('active');document.querySelectorAll('.nav-item').forEach(e=>e.classList.remove('active'));var nav=document.querySelector('[data-page=\"'+p+'\"]');if(nav)nav.classList.add('active');document.getElementById('sidebar').classList.remove('open');document.querySelector('.sidebar-overlay').classList.remove('active');}");
    html += F("function navigateTo(p){history.pushState(null,'',location.pathname+'#/'+p);showPage(p);}");
    html += F("function loadPage(){var hash=location.hash.replace('#/','');var page=hash||'status';showPage(page);}");
    html += F("window.addEventListener('popstate',loadPage);");
    html += F("function toggleTheme(){var t=document.documentElement.getAttribute('data-theme')==='dark'?'light':'dark';document.documentElement.setAttribute('data-theme',t);localStorage.setItem('theme',t);}var st=localStorage.getItem('theme');if(st)document.documentElement.setAttribute('data-theme',st);");
    html += F("function toggleSidebar(){document.getElementById('sidebar').classList.toggle('open');document.querySelector('.sidebar-overlay').classList.toggle('active');}");
    html += F("window.onload=function(){loadPage();var qd=document.getElementById('qrcode');if(qd&&typeof qrcode!=='undefined'){try{var qr=qrcode(0,'M');qr.addData('"); html += homekit_qr_uri; html += F("');qr.make();qd.innerHTML=qr.createImgTag(4,0);}catch(e){}}};");
    html += F("function scanWifi(){var s=document.getElementById('wifiSelect');s.innerHTML='<option>Scanning...</option>';fetch('/api/scan').then(r=>r.json()).then(d=>{s.innerHTML='<option value=\"\">-- Select --</option>';d.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{s.innerHTML+='<option value=\"'+n.ssid+'\">'+n.ssid+' ('+n.rssi+')</option>';});}).catch(()=>{s.innerHTML='<option>Failed</option>';});}");
    html += F("function addTest(t){var s=document.getElementById('test-status');if(s)s.innerHTML='Adding...';fetch('/api/test?type='+t).then(r=>r.json()).then(d=>{if(s)s.innerHTML=d.message;setTimeout(()=>{navigateTo('devices');setTimeout(()=>location.reload(),100);},2000);});}");
    html += F("function renameDevice(id,name){var n=prompt('New name:',name);if(n&&n!==name){fetch('/api/rename?id='+encodeURIComponent(id)+'&name='+encodeURIComponent(n)).then(r=>r.json()).then(d=>{alert(d.message);location.reload();});}}");
    html += F("function removeDevice(id){if(confirm('Remove '+id+'?')){fetch('/api/remove?id='+encodeURIComponent(id)).then(r=>r.json()).then(d=>{alert(d.message);location.reload();});}}");
    html += F("function setSensorType(id,sensor,type){fetch('/api/settype?id='+encodeURIComponent(id)+'&sensor='+sensor+'&type='+type).then(r=>r.json()).then(d=>{alert(d.message);if(d.success){setTimeout(()=>location.reload(),1000);}});}");
    html += F("function unpairHomeKit(){if(confirm('Unpair?')){fetch('/api/unpair').then(()=>{alert('Unpairing...');setTimeout(()=>location.reload(),3000);});}}");
    html += F("function restartDevice(){if(confirm('Restart?')){fetch('/api/restart').then(()=>{alert('Restarting...');setTimeout(()=>location.reload(),5000);});}}");
    html += F("function factoryReset(){if(confirm('Reset ALL settings?')){fetch('/reset',{method:'POST'}).then(()=>{alert('Resetting...');});}}");
    html += F("function saveSettings(e){e.preventDefault();var f=new FormData(e.target);fetch('/save',{method:'POST',body:new URLSearchParams(f)}).then(()=>{alert('Saved! Restarting...');setTimeout(()=>location.reload(),5000);});return false;}");
    html += F("function toggleHw(k){fetch('/api/hardware?'+k+'=toggle').then(r=>r.json()).then(d=>{if(k==='pwr_led')document.getElementById('pwrLed').classList.toggle('active',d.pwr_led);if(k==='act_led')document.getElementById('actLed').classList.toggle('active',d.act_led);if(k==='oled_en')document.getElementById('oledEn').classList.toggle('active',d.oled_en);});}");
    html += F("function setHwVal(k,v){fetch('/api/hardware?'+k+'='+v);}");
    html += F("</script></body></html>");
    
    webServer.send(200, "text/html", html);
}

 void handleSave() {
     bool needsRestart = false;
     
     if (webServer.hasArg("ssid") && webServer.arg("ssid").length() > 0) {
         String newSsid = webServer.arg("ssid");
         if (strcmp(wifi_ssid, newSsid.c_str()) != 0) {
             strncpy(wifi_ssid, newSsid.c_str(), 63);
             wifi_ssid[63] = 0;
             needsRestart = true;
         }
     }
     
     if (webServer.hasArg("password") && webServer.arg("password").length() > 0) {
         strncpy(wifi_password, webServer.arg("password").c_str(), 63);
         wifi_password[63] = 0;
         needsRestart = true;
     }
     
     if (webServer.hasArg("freq")) {
         float newFreq = webServer.arg("freq").toFloat();
         if (newFreq != lora_frequency) {
             lora_frequency = newFreq;
             needsRestart = true;
         }
     }
     
     // LoRa radio settings
     if (webServer.hasArg("lora_sf")) {
         uint8_t newSf = webServer.arg("lora_sf").toInt();
         if (newSf != lora_sf) {
             lora_sf = newSf;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_bw")) {
         uint32_t newBw = webServer.arg("lora_bw").toInt();
         if (newBw != lora_bw) {
             lora_bw = newBw;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_cr")) {
         uint8_t newCr = webServer.arg("lora_cr").toInt();
         if (newCr != lora_cr) {
             lora_cr = newCr;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_pre")) {
         uint16_t newPre = webServer.arg("lora_pre").toInt();
         if (newPre != lora_preamble) {
             lora_preamble = newPre;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_sync") && webServer.arg("lora_sync").length() > 0) {
         uint8_t newSync = strtol(webServer.arg("lora_sync").c_str(), NULL, 16);
         if (newSync != lora_syncword) {
             lora_syncword = newSync;
             needsRestart = true;
         }
     }
     
     if (webServer.hasArg("gw_key")) {
         strncpy(gateway_key, webServer.arg("gw_key").c_str(), 31);
         gateway_key[31] = 0;
     }
     
     if (webServer.hasArg("enc_mode")) {
         encryption_mode = webServer.arg("enc_mode").toInt();
     }
     
     if (webServer.hasArg("enc_key") && webServer.arg("enc_key").length() >= 2) {
         String keyHex = webServer.arg("enc_key");
         encrypt_key_len = min((int)(keyHex.length() / 2), 16);
         for (int i = 0; i < encrypt_key_len; i++) {
             char hex[3] = { keyHex[i*2], keyHex[i*2+1], 0 };
             encrypt_key[i] = strtol(hex, NULL, 16);
         }
     }
     
     saveSettings();
     
     String html = "<!DOCTYPE html><html><head>";
     html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
     html += "<style>";
     html += "body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}";
     html += ".box{text-align:center;padding:40px}";
     html += "h1{color:#4ecdc4;font-size:3em;margin:0}";
     html += "p{color:#aaa;margin-top:16px}";
     html += ".spinner{width:40px;height:40px;border:3px solid rgba(255,255,255,0.1);border-top:3px solid #4ecdc4;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto}";
     html += "@keyframes spin{to{transform:rotate(360deg)}}";
     html += "</style></head><body><div class=\"box\">";
     html += "<h1>OK</h1>";
     html += "<p>Settings saved!</p>";
     html += "<div class=\"spinner\"></div>";
     html += "<p>Restarting...</p>";
     html += "</div>";
     
     if (needsRestart || ap_mode) {
         html += "<script>setTimeout(function(){";
         if (ap_mode && strlen(wifi_ssid) > 0) {
             // If we were in AP mode and WiFi is now configured, 
             // we can't redirect since the device will be on a different network
             html += "document.body.innerHTML='<div class=\"box\"><h1>OK</h1><p>Settings saved!</p><p style=\"color:#4ecdc4\">Connect to your WiFi network and find the bridge at its new IP.</p></div>';";
         } else {
             html += "location.href='/';";
         }
         html += "},5000);</script>";
     } else {
         html += "<script>setTimeout(function(){location.href='/';},2000);</script>";
     }
     
     html += "</body></html>";
     
     webServer.send(200, "text/html", html);
     
     delay(1000);
     
     if (needsRestart || ap_mode) {
         ESP.restart();
     }
 }
 
 void handleReset() {
     clearSettings();
     
     String html = "<!DOCTYPE html><html><head>";
     html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
     html += "<style>";
     html += "body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}";
     html += ".box{text-align:center;padding:40px}";
     html += "h1{color:#e74c3c;font-size:3em;margin:0}";
     html += "p{color:#aaa;margin-top:16px}";
     html += ".spinner{width:40px;height:40px;border:3px solid rgba(255,255,255,0.1);border-top:3px solid #e74c3c;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto}";
     html += "@keyframes spin{to{transform:rotate(360deg)}}";
     html += "</style></head><body><div class=\"box\">";
     html += "<h1>RESET</h1>";
     html += "<p>Factory reset complete!</p>";
     html += "<div class=\"spinner\"></div>";
     html += "<p>Restarting into setup mode...</p>";
     html += "<p style=\"color:#666;font-size:0.9em\">Connect to: " + String(AP_SSID) + "</p>";
     html += "</div>";
     html += "<script>setTimeout(function(){},5000);</script>";
     html += "</body></html>";
     
     webServer.send(200, "text/html", html);
     
     delay(1000);
     ESP.restart();
 }
 
 void handleScan() {
     Serial.println("[WIFI] Scanning networks...");
     
     int n = WiFi.scanNetworks();
     
     StaticJsonDocument<1024> doc;
     JsonArray networks = doc.createNestedArray("networks");
     
     for (int i = 0; i < n && i < 15; i++) {
         JsonObject net = networks.createNestedObject();
         net["ssid"] = WiFi.SSID(i);
         net["rssi"] = WiFi.RSSI(i);
         net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
     }
     
     WiFi.scanDelete();
     
     String output;
     serializeJson(doc, output);
     webServer.send(200, "application/json", output);
     
     Serial.printf("[WIFI] Found %d networks\n", n);
 }
 
 // Test device handler - creates simulated sensors for testing
 void handleTestDevice() {
     String type = webServer.arg("type");
     String response;
     StaticJsonDocument<256> responseDoc;
     
     Serial.printf("[TEST] Creating test device type: %s\n", type.c_str());
     
     // Generate a unique test device ID
     static int testCounter = 0;
     testCounter++;
     String deviceId = "Test_" + type + "_" + String(testCounter);
     
     // Create a fake JSON document simulating a LoRa packet
     StaticJsonDocument<256> doc;
     doc["k"] = gateway_key;  // Use configured gateway key
     doc["id"] = deviceId;
     
     if (type == "temp") {
         doc["t"] = 22.5 + (random(0, 100) / 10.0);  // Random temp 22.5-32.5
         doc["b"] = 85;
     } else if (type == "humidity") {
         doc["hu"] = 45 + random(0, 30);  // Random humidity 45-75%
         doc["b"] = 90;
     } else if (type == "temp_hum") {
         doc["t"] = 21.0 + (random(0, 80) / 10.0);
         doc["hu"] = 40 + random(0, 40);
         doc["b"] = 75;
     } else if (type == "motion") {
         doc["m"] = true;
         doc["b"] = 100;
     } else if (type == "contact") {
         doc["c"] = false;  // false = closed/normal
         doc["b"] = 95;
     } else if (type == "light") {
         doc["l"] = 100 + random(0, 900);  // Random lux 100-1000
         doc["b"] = 80;
     } else if (type == "full") {
         doc["t"] = 23.5;
         doc["hu"] = 55;
         doc["l"] = 500;
         doc["b"] = 70;
     } else if (type == "update") {
         // Update existing test devices with new random values
         for (int i = 0; i < device_count; i++) {
             if (strncmp(devices[i].id, "Test_", 5) == 0) {
                 if (devices[i].has_temp) {
                     devices[i].temperature = 20.0 + (random(0, 100) / 10.0);
                     if (devices[i].tempChar) devices[i].tempChar->setVal(devices[i].temperature);
                 }
                 if (devices[i].has_hum) {
                     devices[i].humidity = 40 + random(0, 40);
                     if (devices[i].humChar) devices[i].humChar->setVal(devices[i].humidity);
                 }
                 if (devices[i].has_light) {
                     devices[i].lux = 100 + random(0, 900);
                     if (devices[i].lightChar) devices[i].lightChar->setVal(max(0.0001f, (float)devices[i].lux));
                 }
                 if (devices[i].has_motion) {
                     devices[i].motion = !devices[i].motion;
                     if (devices[i].motionChar) devices[i].motionChar->setVal(devices[i].motion);
                 }
                 if (devices[i].has_contact) {
                     devices[i].contact = !devices[i].contact;
                     if (devices[i].contactChar) devices[i].contactChar->setVal(devices[i].contact ? 0 : 1);
                 }
                 Serial.printf("[TEST] Updated device: %s\n", devices[i].id);
             }
         }
         responseDoc["success"] = true;
         responseDoc["message"] = "Updated all test devices!";
         serializeJson(responseDoc, response);
         webServer.send(200, "application/json", response);
         return;
     } else {
         responseDoc["success"] = false;
         responseDoc["message"] = "Unknown test type: " + type;
         serializeJson(responseDoc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     // Find or register the device (simulating LoRa packet processing)
     Device* dev = findDevice(deviceId.c_str());
     if (!dev) {
         dev = registerDevice(deviceId.c_str(), doc);
     }
     
     if (dev) {
         updateDevice(dev, doc, -50);  // Fake RSSI of -50
         packets_received++;
         last_packet_time = millis();
         last_event = "Test: " + deviceId;
         
         responseDoc["success"] = true;
         responseDoc["message"] = "Created " + deviceId + " - check Home app!";
         responseDoc["device_id"] = deviceId;
     } else {
         responseDoc["success"] = false;
         responseDoc["message"] = "Failed to create device (max reached?)";
     }
     
     serializeJson(responseDoc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Unpair HomeKit handler - removes all paired controllers
 void handleUnpair() {
     Serial.println("[HOMEKIT] Unpairing all controllers...");
     
     // HomeSpan's unpair command
     homeSpan.processSerialCommand("U");
     
     webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Unpaired. Restarting...\"}");
     
     delay(1000);
     ESP.restart();
 }
 
 // Rename device handler
 void handleRenameDevice() {
     String id = webServer.arg("id");
     String newName = webServer.arg("name");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0 || newName.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing id or name parameter";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (renameDevice(id.c_str(), newName.c_str())) {
         doc["success"] = true;
         doc["message"] = "Renamed to: " + newName;
     } else {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Remove device handler
 void handleRemoveDevice() {
     String id = webServer.arg("id");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing id parameter";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (removeDevice(id.c_str())) {
         doc["success"] = true;
         doc["message"] = "Device removed from HomeKit.";
     } else {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Restart device handler
 void handleRestart() {
     webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
     delay(500);
     ESP.restart();
 }
 
 // Set sensor type handler
 void handleSetSensorType() {
     String id = webServer.arg("id");
     String sensor = webServer.arg("sensor");
     String typeStr = webServer.arg("type");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0 || sensor.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing parameters";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     Device* dev = findDevice(id.c_str());
     if (!dev) {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
         String response;
         serializeJson(doc, response);
         webServer.send(404, "application/json", response);
         return;
     }
     
     uint8_t newType = typeStr.toInt();
     bool changed = false;
     String typeName;
     
     if (sensor == "contact" && dev->has_contact) {
         if (dev->contact_type != newType) {
             dev->contact_type = newType;
             changed = true;
             typeName = getContactTypeName(newType);
         }
     } else if (sensor == "motion" && dev->has_motion) {
         if (dev->motion_type != newType) {
             dev->motion_type = newType;
             changed = true;
             typeName = getMotionTypeName(newType);
         }
     } else {
         doc["success"] = false;
         doc["message"] = "Invalid sensor type";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (changed) {
         saveDevices();
         
         // Delete old accessory and recreate with new type
         if (dev->aid > 0 && homekit_started) {
             uint32_t oldAid = dev->aid;
             Serial.printf("[HOMEKIT] Changing sensor type for %s to %s (type=%d, old AID=%d)\n", 
                           id.c_str(), typeName.c_str(), newType, oldAid);
             
             homeSpan.deleteAccessory(dev->aid);
             
             // Create a temporary spacer accessory to consume the old AID
             // This forces the real accessory to get a new AID
             SpanAccessory* spacer = new SpanAccessory();
             uint32_t spacerAid = spacer->getAID();
             Serial.printf("[HOMEKIT] Created spacer with AID=%d\n", spacerAid);
             
             // Update database after deletion
             homeSpan.updateDatabase();
             Serial.println("[HOMEKIT] Database updated after deletion");
             
             // Clear pointers
             dev->aid = 0;
             dev->nameChar = nullptr;
             dev->tempChar = nullptr;
             dev->humChar = nullptr;
             dev->battChar = nullptr;
             dev->lightChar = nullptr;
             dev->motionChar = nullptr;
             dev->contactChar = nullptr;
             
             // Small delay to let HomeKit process the deletion
             delay(100);
             
             // Recreate with new type (will get a new AID since spacer took the old one)
             Serial.printf("[HOMEKIT] Recreating accessory with motion_type=%d\n", dev->motion_type);
             createHomekitAccessory(dev);
             
             // Now delete the spacer
             homeSpan.deleteAccessory(spacerAid);
             homeSpan.updateDatabase();
             Serial.printf("[HOMEKIT] Deleted spacer, new accessory AID=%d\n", dev->aid);
         }
         
         doc["success"] = true;
         doc["message"] = "Changed to " + typeName + " sensor";
     } else {
         doc["success"] = true;
         doc["message"] = "No change needed";
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Hardware settings handler
 void handleHardwareSettings() {
     // Handle toggle actions
     if (webServer.hasArg("pwr_led")) {
         if (webServer.arg("pwr_led") == "toggle") {
             power_led_enabled = !power_led_enabled;
         } else {
             power_led_enabled = webServer.arg("pwr_led") == "1";
         }
         if (!power_led_enabled) {
             digitalWrite(LED_PIN, HIGH);
         }
         saveSettings();
     }
     
     if (webServer.hasArg("act_led")) {
         if (webServer.arg("act_led") == "toggle") {
             activity_led_enabled = !activity_led_enabled;
         } else {
             activity_led_enabled = webServer.arg("act_led") == "1";
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_en")) {
         if (webServer.arg("oled_en") == "toggle") {
             oled_enabled = !oled_enabled;
         } else {
             oled_enabled = webServer.arg("oled_en") == "1";
         }
         if (!oled_enabled && display_available) {
             display.displayOff();
         } else if (oled_enabled && display_available) {
             display.displayOn();
             oled_is_off = false;
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_br")) {
         oled_brightness = webServer.arg("oled_br").toInt();
         if (display_available) {
             display.setBrightness(oled_brightness);
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_to")) {
         oled_timeout = webServer.arg("oled_to").toInt();
         saveSettings();
     }
     
     StaticJsonDocument<256> doc;
     doc["pwr_led"] = power_led_enabled;
     doc["act_led"] = activity_led_enabled;
     doc["oled_en"] = oled_enabled;
     doc["oled_br"] = oled_brightness;
     doc["oled_to"] = oled_timeout;
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Captive portal handler - redirect all requests to root
 void handleNotFound() {
     if (ap_mode) {
         webServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
         webServer.send(302, "text/plain", "");
     } else {
         webServer.send(404, "text/plain", "Not Found");
     }
 }
 

// ============== Setup Function ==============
void setupWebServer() {
    // Main page
    webServer.on("/", handleRoot);

    // API endpoints
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.on("/reset", HTTP_POST, handleReset);
    webServer.on("/api/scan", handleScan);
    webServer.on("/api/test", handleTestDevice);
    webServer.on("/api/unpair", handleUnpair);
    webServer.on("/api/rename", handleRenameDevice);
    webServer.on("/api/remove", handleRemoveDevice);
    webServer.on("/api/restart", handleRestart);
    webServer.on("/api/settype", handleSetSensorType);
    webServer.on("/api/hardware", handleHardwareSettings);
    webServer.onNotFound(handleNotFound);
    webServer.begin();

    Serial.println("[WEBSERVER] Started on port 80");
}
