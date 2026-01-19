// http_client.cpp - Implementação centralizada e otimizada
#include "http_client.h"
#include "debug_config.h"  // Adicionado para ter DEBUG_HTTP

// Instância global
FermentadorHTTPClient httpClient;

FermentadorHTTPClient::FermentadorHTTPClient() {}

FermentadorHTTPClient::~FermentadorHTTPClient() {
    http.end();
}

// =====================================================
// FUNÇÃO CENTRAL DE REQUISIÇÃO (Único local com String payload)
// =====================================================
bool FermentadorHTTPClient::makeRequest(const String& endpoint, const String& method, 
                                        const JsonDocument* payloadDoc, String& response) {
    if (!isConnected()) {
        #if DEBUG_HTTP
        Serial.println(F("[HTTP] ❌ Cliente não conectado"));
        #endif
        return false;
    }

    HTTPClient http;
    String url = String(SERVER_URL) + endpoint;
    
    #if DEBUG_HTTP
    Serial.printf("[HTTP] 🌐 %s %s\n", method.c_str(), url.c_str());
    #endif
    
    yield();
    http.begin(wifiClient, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT);

    int httpCode = -1;

    if (method == "GET") {
        httpCode = http.GET();
    } else if (method == "POST") {
        if (payloadDoc != nullptr) {
            // OTIMIZAÇÃO: Reserva RAM exata antes de serializar para evitar fragmentação
            String payload;
            payload.reserve(measureJson(*payloadDoc) + 1);
            serializeJson(*payloadDoc, payload);
            httpCode = http.POST(payload);
        } else {
            httpCode = http.POST("");
        }
    }

    yield();
    
    #if DEBUG_HTTP
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        Serial.println(F("[HTTP] ✅ Requisição bem-sucedida"));
    } else {
        Serial.printf("[HTTP] ❌ Código HTTP: %d\n", httpCode);
    }
    #endif
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        response = http.getString();
        http.end();
        return true;
    }

    http.end();
    return false;
}

// =====================================================
// MÉTODOS DE FERMENTAÇÃO
// =====================================================

bool FermentadorHTTPClient::getActiveFermentation(JsonDocument& doc) {
    String response;
    yield();
    // CORREÇÃO: Passar nullptr em vez de "" para requisições GET [1]
    if (!makeRequest("api/esp/active.php", "GET", nullptr, response)) {
        #if DEBUG_HTTP
        Serial.println(F("[HTTP] ❌ Falha em getActiveFermentation"));
        #endif
        return false;
    }
    
    yield();
    DeserializationError error = deserializeJson(doc, response);
    
    #if DEBUG_HTTP
    if (error) {
        Serial.printf("[HTTP] ❌ JSON parse error: %s\n", error.c_str());
    }
    #endif
    
    return !error;
}

bool FermentadorHTTPClient::getConfiguration(const char* configId, JsonDocument& doc) {
    String endpoint = "api/esp/config.php?id=" + String(configId);
    String response;
    yield();
    // CORREÇÃO: Passar nullptr em vez de "" [2]
    if (!makeRequest(endpoint, "GET", nullptr, response)) {
        #if DEBUG_HTTP
        Serial.println(F("[HTTP] ❌ Falha em getConfiguration"));
        #endif
        return false;
    }
    
    yield();
    DeserializationError error = deserializeJson(doc, response);
    
    #if DEBUG_HTTP
    if (error) {
        Serial.printf("[HTTP] ❌ JSON parse error: %s\n", error.c_str());
    }
    #endif
    
    return !error;
}

bool FermentadorHTTPClient::updateFermentationState(const char* configId, const JsonDocument& doc) {
    String endpoint = "api/esp/state.php?config_id=" + String(configId);
    String response;
    yield();
    // CORREÇÃO: Passa o endereço do doc diretamente para o makeRequest [3, 4]
    bool result = makeRequest(endpoint, "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Estado atualizado"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao atualizar estado"));
    }
    #endif
    
    return result;
}

// =====================================================
// MÉTODOS DE LEITURA E CONTROLE
// =====================================================

bool FermentadorHTTPClient::sendReading(const char* configId, float tempFridge, 
                                        float tempFermenter, float tempTarget, float gravity) {
    JsonDocument doc;
    if (configId != nullptr && strlen(configId) > 0) {
        doc["config_id"] = configId;
    }
    doc["temp_fridge"] = tempFridge;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_target"] = tempTarget;
    if (gravity > 0.01) doc["gravity"] = gravity;

    String response;
    // Otimizado: envia o endereço do doc [5]
    bool result = makeRequest("api/esp/reading.php", "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Leitura enviada"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao enviar leitura"));
    }
    #endif
    
    return result;
}

bool FermentadorHTTPClient::updateControlState(const char* configId, float setpoint, 
                                               bool cooling, bool heating) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["setpoint"] = setpoint;
    doc["cooling"] = cooling;
    doc["heating"] = heating;

    String response;
    yield();
    // CORREÇÃO: Removida serialização local para String. Passa &doc [6]
    bool result = makeRequest("api/esp/control.php", "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Estado de controle atualizado"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao atualizar controle"));
    }
    #endif
    
    return result;
}

bool FermentadorHTTPClient::notifyTargetReached(const char* configId) {
    JsonDocument doc;
    doc["config_id"] = configId;
    doc["target_reached"] = true;

    String response;
    yield();
    // CORREÇÃO: Passa &doc diretamente [7]
    bool result = makeRequest("api/esp/target.php", "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Target reached notificado"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao notificar target"));
    }
    #endif
    
    return result;
}

// =====================================================
// SENSORES E ISPINDEL
// =====================================================

bool FermentadorHTTPClient::updateCurrentTemperatures(float tempFermenter, float tempFridge) {
    JsonDocument doc;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_fridge"] = tempFridge;

    String response;
    yield();
    // CORREÇÃO: Passa &doc em vez de payload String [3]
    bool result = makeRequest("api/esp/sensors.php?action=update_temperatures", "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Temperaturas atualizadas"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao atualizar temperaturas"));
    }
    #endif
    
    return result;
}

bool FermentadorHTTPClient::sendSensors(const JsonDocument& sensorsDoc) {
    String response;
    yield();
    // Passa o endereço do documento diretamente
    return makeRequest("api/esp/sensors.php?action=save_detected", "POST", &sensorsDoc, response);
}

bool FermentadorHTTPClient::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool FermentadorHTTPClient::getAssignedSensors(String& fermenterAddr, String& fridgeAddr) {
    String response;
    String endpoint = "api/esp/sensors.php?action=get_assigned";
    
    // ✅ YIELD 1: Mantido para estabilidade do Watchdog no ESP8266 [1]
    yield();
    
    // CORREÇÃO: Usamos nullptr no lugar de "" para coincidir com const JsonDocument* payloadDoc
    // Isso evita a criação de um objeto String desnecessário para uma requisição GET [1, 2]
    if (!makeRequest(endpoint, "GET", nullptr, response)) {
        #if DEBUG_HTTP
        Serial.println(F("[HTTP] Erro ao buscar sensores configurados"));
        #endif
        return false;
    }
    
    JsonDocument doc; // Gerenciamento automático da ArduinoJson v7 [3]
    
    // ✅ YIELD 2: Antes de processar o JSON pesado
    yield();
    
    DeserializationError error = deserializeJson(doc, response);
    
    // ✅ YIELD 3: Após o processamento
    yield();
    
    if (error) {
        #if DEBUG_HTTP
        Serial.printf("[HTTP] JSON erro ao parsear sensores: %s\n", error.c_str());
        #endif
        return false;
    }
    
    // Respeita a verificação de sucesso do seu servidor
    if (!doc["success"].as<bool>()) {
        #if DEBUG_HTTP
        Serial.println(F("[HTTP] Servidor reportou falha"));
        #endif
        return false;
    }
    
    JsonObject sensors = doc["sensors"];
    
    // Inicializa strings como vazias para evitar lixo de memória
    fermenterAddr = "";
    fridgeAddr = "";
    
    // Mapeia as chaves específicas do seu banco de dados
    if (sensors["sensor_fermentador"].is<String>()) {
        fermenterAddr = sensors["sensor_fermentador"].as<String>();
        #if DEBUG_HTTP
        Serial.printf("[HTTP] Sensor fermentador: %s\n", fermenterAddr.c_str());
        #endif
    }
    
    if (sensors["sensor_geladeira"].is<String>()) {
        fridgeAddr = sensors["sensor_geladeira"].as<String>();
        #if DEBUG_HTTP
        Serial.printf("[HTTP] Sensor geladeira: %s\n", fridgeAddr.c_str());
        #endif
    }
    
    // ✅ YIELD 4: Finalização da tarefa
    yield();
    
    return !fermenterAddr.isEmpty() || !fridgeAddr.isEmpty();
}

bool FermentadorHTTPClient::sendHeartbeat(int configId, const DetailedControlStatus& status, 
                                          temperature beerTemp, temperature fridgeTemp) {
    JsonDocument doc; // ArduinoJson v7 gerenciando memória [9]

    // Dados de Identificação e Saúde [1]
    doc["config_id"] = configId;
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();

    // Temperaturas convertidas de Fixed-Point para Float [5, 10, 11]
    if (beerTemp != INVALID_TEMP) doc["temp_fermenter"] = tempToFloat(beerTemp);
    if (fridgeTemp != INVALID_TEMP) doc["temp_fridge"] = tempToFloat(fridgeTemp);

    // Estado dos Atuadores (Relés) [5, 12]
    doc["cooler_active"] = status.coolerActive ? 1 : 0;
    doc["heater_active"] = status.heaterActive ? 1 : 0;

    // Status Detalhado do Controle BrewPi [6, 13]
    JsonObject ctrl = doc["control_status"].to<JsonObject>();
    ctrl["state"] = status.stateName;
    ctrl["is_waiting"] = status.isWaiting;

    if (status.isWaiting) {
        ctrl["wait_seconds"] = status.waitTimeRemaining;
        ctrl["wait_reason"] = status.waitReason;
        
        // Formatação do tempo de espera sem usar Strings pesadas [7, 14, 15]
        char waitDisplay[16];
        if (status.waitTimeRemaining < 60) {
            snprintf(waitDisplay, sizeof(waitDisplay), "%us", status.waitTimeRemaining);
        } else {
            snprintf(waitDisplay, sizeof(waitDisplay), "%um", status.waitTimeRemaining / 60);
        }
        ctrl["wait_display"] = waitDisplay;
    }

    if (status.peakDetection) {
        ctrl["peak_detection"] = true;
        ctrl["estimated_peak"] = status.estimatedPeak;
    }

    // Envio Otimizado: Passa o endereço do documento para o makeRequest [3, 4]
    String response;
    bool result = makeRequest("api.php?path=heartbeat", "POST", &doc, response);
    
    #if DEBUG_HTTP
    if (result) {
        Serial.println(F("[HTTP] ✅ Heartbeat enviado"));
    } else {
        Serial.println(F("[HTTP] ❌ Falha ao enviar heartbeat"));
    }
    #endif
    
    return result;
}

bool FermentadorHTTPClient::sendSensorsData(const JsonDocument& sensorsDoc) {
    String response;
    yield();
    return makeRequest("api/esp/sensors.php?action=save_detected", 
                      "POST", &sensorsDoc, response);
}