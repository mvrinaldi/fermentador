#pragma once

#include <ESP8266WebServer.h>
#include "secrets.h"

// Funções de callback
void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void onOTAEnd(bool success);

// Função de configuração
void setupOTA(ESP8266WebServer &server);

// Funções de controle OTA (habilitar/desabilitar)
void setOTAEnabled(bool enabled);
bool isOTAEnabled();