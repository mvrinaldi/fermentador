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
    if (!isConnected()) {
        Serial.println(F("[HTTP] ❌ Cliente não conectado"));
        return false;
    }

    HTTPClient http;
    String url = String(SERVER_URL) + endpoint;
    
    Serial.println(F("\n[HTTP] ================================================"));
    Serial.printf("[HTTP] 🌐 Requisição: %s %s\n", method.c_str(), endpoint.c_str());
    Serial.printf("[HTTP] URL completa: %s\n", url.c_str());
    
    http.begin(wifiClient, url);
    http.addHeader("Content-Type", "application/json");
    
    if (payload.length() > 0) {
        Serial.println(F("[HTTP] Payload:"));
        Serial.println(payload);
    }
    
    int httpCode = -1;
    
    if (method == "GET") {
        httpCode = http.GET();
    } else if (method == "POST") {
        httpCode = http.POST(payload);
    } else if (method == "PUT") {
        httpCode = http.PUT(payload);
    }
    
    Serial.printf("[HTTP] 📡 Código HTTP: %d\n", httpCode);
    
    if (httpCode > 0) {
        response = http.getString();
        Serial.printf("[HTTP] 📦 Response length: %d bytes\n", response.length());
        
        if (httpCode == HTTP_CODE_OK) {
            Serial.println(F("[HTTP] ✅ Sucesso (200 OK)"));
            Serial.println(F("[HTTP] Response:"));
            Serial.println(response);
            http.end();
            return true;
        } else {
            Serial.printf("[HTTP] ⚠️ Código não-OK: %d\n", httpCode);
            Serial.println(F("[HTTP] Response body:"));
            Serial.println(response);
        }
    } else {
        Serial.printf("[HTTP] ❌ Erro na requisição: %s\n", http.errorToString(httpCode).c_str());
    }
    
    Serial.println(F("[HTTP] ================================================\n"));
    http.end();
    return false;
}

bool FermentadorHTTPClient::getActiveFermentation(JsonDocument& doc) {
    String response;
    
    Serial.println(F("[HTTP] 🔍 Iniciando getActiveFermentation()"));
    
    if (!makeRequest("api/esp/active.php", "GET", "", response)) {
        Serial.println(F("[HTTP] ❌ makeRequest() retornou false"));
        Serial.print(F("[HTTP] Response recebida (mesmo com erro): '"));
        Serial.print(response);
        Serial.println("'");
        return false;
    }
    
    Serial.println(F("[HTTP] ✅ makeRequest() retornou true"));
    Serial.print(F("[HTTP] Response length: "));
    Serial.println(response.length());
    Serial.println(F("[HTTP] Response completa:"));
    Serial.println(F("--- INÍCIO ---"));
    Serial.println(response);
    Serial.println(F("--- FIM ---"));
    
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[HTTP] ❌ JSON erro: %s\n", error.c_str());
        Serial.println(F("[HTTP] Tentando identificar o problema:"));
        
        // Debug do JSON recebido
        for (size_t i = 0; i < response.length() && i < 200; i++) {
            char c = response[i];
            if (c < 32 || c > 126) {
                Serial.printf("[%02X]", (unsigned char)c);
            } else {
                Serial.print(c);
            }
        }
        Serial.println();
        
        return false;
    }
    
    Serial.println(F("[HTTP] ✅ JSON parseado com sucesso"));
    Serial.println(F("[HTTP] Conteúdo do documento:"));
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
    return true;
}

bool FermentadorHTTPClient::getConfiguration(const char* configId, JsonDocument& doc) {
    String endpoint = "api/esp/config.php?id=" + String(configId);
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

// ==================== ENVIAR LEITURA COMPLETA ====================
bool FermentadorHTTPClient::sendReading(const char* configId, float tempFridge, 
                                       float tempFermenter, float tempTarget, float gravity) {
    JsonDocument doc;
    
    // config_id é opcional - servidor busca fermentação ativa se vazio
    if (configId != nullptr && strlen(configId) > 0) {
        doc["config_id"] = configId;
    }
    
    doc["temp_fridge"] = tempFridge;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_target"] = tempTarget;
    
    if (gravity > 0) {
        doc["gravity"] = gravity;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    String endpoint = "api/esp/reading.php";
    return makeRequest(endpoint, "POST", payload, response);
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
    return makeRequest("api/esp/control.php", "POST", payload, response);
}

bool FermentadorHTTPClient::sendSensors(const String& sensorsJson) {
    String response;
    String endpoint = "api/esp/sensors.php?action=save_detected";
    return makeRequest(endpoint, "POST", sensorsJson, response);
}

bool FermentadorHTTPClient::notifyTargetReached(const char* configId) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["target_reached"] = true;
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return makeRequest("api/esp/target.php", "POST", payload, response);
}

bool FermentadorHTTPClient::updateCurrentTemperatures(float tempFermenter, float tempFridge) {
    JsonDocument doc;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_fridge"] = tempFridge;
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return makeRequest("api/esp/sensors.php?action=update_temperatures", "POST", payload, response);
}

bool FermentadorHTTPClient::updateFermentationState(const char* configId, const String& stateJson) {
    String endpoint = "api/esp/state.php?config_id=" + String(configId);
    String response;
    return makeRequest(endpoint, "POST", stateJson, response);
}

bool FermentadorHTTPClient::sendSpindelData(const String& spindelJson) {
    String response;
    return makeRequest("api/esp/ispindel.php", "POST", spindelJson, response);
}

bool FermentadorHTTPClient::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void FermentadorHTTPClient::printError(const char* context) {
    Serial.printf("[HTTP] Erro em %s\n", context);
}

// ==================== BUSCAR SENSORES CONFIGURADOS ====================
bool FermentadorHTTPClient::getAssignedSensors(String& fermenterAddr, String& fridgeAddr) {
    String response;
    String endpoint = "api/esp/sensors.php?action=get_assigned";
    
    if (!makeRequest(endpoint, "GET", "", response)) {
        Serial.println(F("[HTTP] Erro ao buscar sensores configurados"));
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        Serial.printf("[HTTP] JSON erro ao parsear sensores: %s\n", error.c_str());
        return false;
    }
    
    if (!doc["success"].as<bool>()) {
        Serial.println(F("[HTTP] Servidor reportou falha"));
        return false;
    }
    
    JsonObject sensors = doc["sensors"];
    
    // Inicializa vazios
    fermenterAddr = "";
    fridgeAddr = "";
    
    if (sensors["sensor_fermentador"].is<String>()) {
        fermenterAddr = sensors["sensor_fermentador"].as<String>();
        Serial.printf("[HTTP] Sensor fermentador: %s\n", fermenterAddr.c_str());
    }
    
    if (sensors["sensor_geladeira"].is<String>()) {
        fridgeAddr = sensors["sensor_geladeira"].as<String>();
        Serial.printf("[HTTP] Sensor geladeira: %s\n", fridgeAddr.c_str());
    }
    
    return !fermenterAddr.isEmpty() || !fridgeAddr.isEmpty();
}