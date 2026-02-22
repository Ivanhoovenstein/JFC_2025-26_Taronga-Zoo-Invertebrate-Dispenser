#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"

// ========================================
// Web Server Functions
// ========================================

void setupCaptivePortal();
void registerRoutes();
void serveStaticFile(String path, String type);
void setCORSHeaders();

// ========================================
// Global Server Objects (extern)
// ========================================

extern WebServer server;
extern DNSServer dnsServer;

#endif // WEB_SERVER_H
