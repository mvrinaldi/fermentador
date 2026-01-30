// ota.h
#pragma once

#include <ESP8266WebServer.h>

void setupOTA(ESP8266WebServer &server);
void handleOTA();
bool isOTAInitialized();
bool isOTAInProgress();
