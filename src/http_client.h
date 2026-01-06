// http_client.h - Cliente HTTP otimizado para ESP8266
#pragma once

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "ESP8266WiFi.h"

// ========== CONFIGURAÇÕES ==========
#define API_BASE_URL "http://fermentador.mvrinaldi.com.br/api/esp/"
#define HTTP_TIMEOUT 5000 // 5 segundos
#define MAX_RETRIES 3

// ========== CLASSE CLIENTE HTTP ==========
class FermentadorHTTPClient {
private:
    WiFiClient wifiClient;
    HTTPClient http;
    
    bool makeRequest(const String& endpoint, const String& method, 
                    const String& payload, String& response);

public:
    FermentadorHTTPClient();
    ~FermentadorHTTPClient();
    
    // Métodos principais
    bool getActiveFermentation(JsonDocument& doc);
    bool getConfiguration(const char* configId, JsonDocument& doc);
    bool sendReading(const char* configId, float tempFridge, float tempFermenter, 
                    float tempTarget, float gravity);
    bool updateControlState(const char* configId, float setpoint, bool cooling, bool heating);
    bool sendSensors(const String& sensorsJson);
    bool notifyTargetReached(const char* configId);
    bool updateFermentationState(const char* configId, const String& stateJson);
    bool sendSpindelData(const String& spindelJson);
    
    // Helpers
    bool isConnected();
    void printError(const char* context);
};

// DECLARAÇÃO EXTERNA (não definição)
extern FermentadorHTTPClient httpClient;