// http_client.h - Cliente HTTP otimizado para ESP8266
#pragma once

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "ESP8266WiFi.h"
#include "BrewPiStructs.h"        // Define o tipo 'temperature' [2]
#include "controle_temperatura.h"   // Define a struct 'DetailedControlStatus'

// ========== CONFIGURAÇÕES ==========
#define SERVER_URL "http://fermentador.mvrinaldi.com.br/"
#define HTTP_TIMEOUT 5000 // 5 segundos
#define MAX_RETRIES 3

// ========== CLASSE CLIENTE HTTP ==========
class FermentadorHTTPClient {
private:
    WiFiClient wifiClient;
    HTTPClient http;

    bool makeRequest(const String& endpoint, const String& method, 
                    const JsonDocument* payloadDoc, String& response);

public:
    FermentadorHTTPClient();
    ~FermentadorHTTPClient();
    
    // ==================== FERMENTAÇÃO ====================
    bool getActiveFermentation(JsonDocument& doc);
    bool getConfiguration(const char* configId, JsonDocument& doc);
    bool updateFermentationState(const char* configId, const JsonDocument& doc);
    bool notifyTargetReached(const char* configId);
    bool updateStageIndex(const char* configId, int newStageIndex);

    // ==================== LEITURAS ====================
    bool sendReading(const char* configId, float tempFridge, 
                    float tempFermenter, float tempTarget);
    
    // ==================== CONTROLE ====================
    bool updateControlState(const char* configId, float setpoint, 
                          bool cooling, bool heating);
    
    // ==================== SENSORES ====================
    bool sendSensors(const JsonDocument& sensorsDoc);
    bool sendSensorsData(const JsonDocument& sensorsDoc);
    bool getAssignedSensors(String& fermenterAddr, String& fridgeAddr);
    bool updateCurrentTemperatures(float tempFermenter, float tempFridge);
    
    // ==================== ISPINDEL ====================
    bool sendSpindelData(const String& spindelJson);
 
    // ==================== UTILIDADES ====================
    bool isConnected();
    void printError(const char* context);

    bool sendHeartbeat(int configId, const DetailedControlStatus& status, temperature beerTemp, temperature fridgeTemp);
};

// Instância global (declarada em http_client.cpp)
extern FermentadorHTTPClient httpClient;