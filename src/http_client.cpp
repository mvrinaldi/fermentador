// http_client.cpp - Implementação do cliente HTTP
#include "http_client.h"

// ========== DEFINIÇÃO DA INSTÂNCIA GLOBAL (APENAS AQUI) ==========
FermentadorHTTPClient httpClient;

// ========== IMPLEMENTAÇÃO DOS MÉTODOS ==========

FermentadorHTTPClient::FermentadorHTTPClient() {
    // Construtor
}

FermentadorHTTPClient::~FermentadorHTTPClient() {
    http.end();
}

bool FermentadorHTTPClient::makeRequest(const String& endpoint, const String& method, 
                                       const String& payload, String& response) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[HTTP] WiFi desconectado"));
        return false;
    }
    
    String url = String(API_BASE_URL) + endpoint;
    
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            Serial.printf("[HTTP] Tentativa %d/%d\n", attempt + 1, MAX_RETRIES);
            delay(1000 * attempt); // Backoff exponencial
        }
        
        http.begin(wifiClient, url);
        http.setTimeout(HTTP_TIMEOUT);
        http.addHeader("Content-Type", "application/json");
        
        int httpCode = -1;
        
        if (method == "GET") {
            httpCode = http.GET();
        } else if (method == "POST") {
            httpCode = http.POST(payload);
        } else if (method == "PUT") {
            httpCode = http.PUT(payload);
        }
        
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                response = http.getString();
                http.end();
                return true;
            } else {
                Serial.printf("[HTTP] Erro %d: %s\n", httpCode, http.getString().c_str());
            }
        } else {
            Serial.printf("[HTTP] Erro de conexão: %s\n", http.errorToString(httpCode).c_str());
        }
        
        http.end();
    }
    
    return false;
}

bool FermentadorHTTPClient::getActiveFermentation(JsonDocument& doc) {
    String response;
    
    if (!makeRequest("active.php", "GET", "", response)) {
        return false;
    }
    
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[HTTP] JSON erro: %s\n", error.c_str());
        return false;
    }
    
    return true;
}

bool FermentadorHTTPClient::getConfiguration(const char* configId, JsonDocument& doc) {
    String endpoint = "config.php?id=" + String(configId);
    String response;
    
    if (!makeRequest(endpoint, "GET", "", response)) {
        return false;
    }
    
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[HTTP] JSON erro: %s\n", error.c_str());
        return false;
    }
    
    return true;
}

bool FermentadorHTTPClient::sendReading(const char* configId, float tempFridge, 
                                       float tempFermenter, float tempTarget, float gravity) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["temp_fridge"] = tempFridge;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_target"] = tempTarget;
    
    if (gravity > 0) {
        doc["gravity"] = gravity;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return makeRequest("reading.php", "POST", payload, response);
}

bool FermentadorHTTPClient::updateControlState(const char* configId, float setpoint, 
                                              bool cooling, bool heating) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["setpoint"] = setpoint;
    doc["cooling"] = cooling;
    doc["heating"] = heating;
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return makeRequest("control.php", "POST", payload, response);
}

bool FermentadorHTTPClient::sendSensors(const String& sensorsJson) {
    String response;
    return makeRequest("sensors.php", "POST", sensorsJson, response);
}

bool FermentadorHTTPClient::notifyTargetReached(const char* configId) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["target_reached"] = true;
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return makeRequest("target.php", "POST", payload, response);
}

bool FermentadorHTTPClient::updateFermentationState(const char* configId, const String& stateJson) {
    String endpoint = "state.php?config_id=" + String(configId);
    String response;
    return makeRequest(endpoint, "POST", stateJson, response);
}

bool FermentadorHTTPClient::sendSpindelData(const String& spindelJson) {
    String response;
    return makeRequest("ispindel.php", "POST", spindelJson, response);
}

bool FermentadorHTTPClient::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void FermentadorHTTPClient::printError(const char* context) {
    Serial.printf("[HTTP] Erro em %s\n", context);
}