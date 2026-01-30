// ispindel_handler.cpp
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include "ispindel_handler.h"
#include "ispindel_struct.h"
#include "debug_config.h"
#include "http_client.h"
#include "controle_fermentacao.h"
#include "definitions.h"  // Para SERVER_URL e isValidString
#include "globais.h"      // Para fermentacaoState

// Definição real da variável global
SpindelData mySpindel;

// Função auxiliar para envio imediato ao MySQL
void sendImmediateToMySQL() {
    if (!(fermentacaoState.active || fermentacaoState.concluidaMantendoTemp)) {
        #if DEBUG_ISPINDEL
        Serial.println(F("[iSpindel] ⏸️  Sem fermentação ativa - não envia MySQL"));
        #endif
        return;
    }
    
    if (!isValidString(fermentacaoState.activeId)) {
        #if DEBUG_ISPINDEL
        Serial.println(F("[iSpindel] ❌ ID inválido - não envia MySQL"));
        #endif
        return;
    }
    
    JsonDocument doc;
    doc["name"] = mySpindel.name;
    doc["temperature"] = mySpindel.temperature;
    doc["gravity"] = mySpindel.gravity;
    doc["battery"] = mySpindel.battery;
    doc["angle"] = mySpindel.angle;
    doc["config_id"] = fermentacaoState.activeId;
    
    WiFiClient wifiClient;
    HTTPClient http;
    
    String url = String(SERVER_URL) + "api.php?path=ispindel/data";
    
    if (!http.begin(wifiClient, url)) {
        #if DEBUG_ISPINDEL
        Serial.println(F("[iSpindel] ❌ Não foi possível iniciar conexão MySQL"));
        #endif
        return;
    }
    
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    
    String payload;
    serializeJson(doc, payload);
    
    #if DEBUG_ISPINDEL
    Serial.printf("[iSpindel] Enviando para MySQL: %s\n", payload.c_str());
    #endif
        
    #if DEBUG_ISPINDEL
    int code = http.POST(payload);
    bool success = (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED);
    if (success) {
        Serial.println(F("[iSpindel] ✅ MySQL: Envio imediato OK"));
    } else {
        Serial.printf("[iSpindel] ❌ Falha MySQL. Código: %d\n", code);
    }
    #endif
    
    http.end();
}

void setupSpindelRoutes(ESP8266WebServer& server) {
    LOG_ISPINDEL("[iSpindel] Rotas do iSpindel inicializadas");
    
    server.on("/gravity", HTTP_POST, [&server]() {
        // Log 1: Quando a requisição é recebida
        LOG_ISPINDEL("[iSpindel] Requisição POST recebida em /gravity");

        if (!server.hasArg("plain")) {
            LOG_ISPINDEL("[iSpindel] Body ausente na requisição");
            server.send(400, "text/plain", "Body ausente");
            return;
        }

        // Log 2: Mostrar os dados brutos recebidos
        String rawData = server.arg("plain");
        LOG_ISPINDEL("[iSpindel] Dados brutos recebidos: " + rawData);

        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            LOG_ISPINDEL("[iSpindel] Falha ao decodificar JSON");
            server.send(400, "text/plain", "JSON inválido");
            return;
        }

        // Extrair valores
        const char* name = doc["name"] | "iSpindel";
        float temperature = doc["temperature"] | 0.0;
        float gravity = doc["gravity"] | 0.0;
        float battery = doc["battery"] | 0.0;
        float angle = doc["angle"] | 0.0;

        // Log 3: Mostrar todos os dados recebidos
        LOG_ISPINDEL("[iSpindel] Dados recebidos - " +
            String(name) + ": " +
            "Temp: " + String(temperature, 2) + "°C, " +
            "Gravity: " + String(gravity, 4) + ", " +
            "Battery: " + String(battery, 2) + "V, " +
            "Angle: " + String(angle, 1) + "°");

        // Atribuir aos valores da estrutura
        strlcpy(mySpindel.name, name, sizeof(mySpindel.name));
        mySpindel.temperature = temperature;
        mySpindel.gravity     = gravity;
        mySpindel.battery     = battery;
        mySpindel.angle       = angle;
        mySpindel.lastUpdate  = millis();
        mySpindel.newDataAvailable = true;

        // Log 4: Confirmação de processamento
        LOG_ISPINDEL("[iSpindel] Dados processados e armazenados");

        // ✅ CHAMADA DA FUNÇÃO DE ENVIO IMEDIATO
        sendImmediateToMySQL();

        server.send(200, "text/plain", "OK");
    });
}
