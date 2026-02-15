// http_commands.h - Gerenciamento de comandos HTTP do servidor
#pragma once
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "http_client.h"
#include "preferences_utils.h"

// ===== Função para marcar comando como executado =====
void markCommandExecuted(int commandId) {
    if (!isHTTPOnline()) {
        Serial.println(F("[CMD] HTTP offline, não foi possível marcar comando"));
        return;
    }
    
    HTTPClient http;
    WiFiClient client;
    
    String url = String(SERVER_URL) + "/api/esp/sensors.php?action=mark_executed";
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["command_id"] = commandId;
    
    String json;
    serializeJson(doc, json);
    
    Serial.printf("[CMD] Marcando comando %d como executado\n", commandId);
    
    int httpCode = http.POST(json);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.println(F("[CMD] ✓ Comando marcado como executado"));
    } else {
        Serial.printf("[CMD] ✗ Erro ao marcar comando: %d\n", httpCode);
    }
    
    http.end();
}

// ===== Função para verificar comandos pendentes =====
void checkPendingCommands() {
    if (!isHTTPOnline()) {
        return;
    }
    
    HTTPClient http;
    WiFiClient client;
    
    String url = String(SERVER_URL) + "/api/esp/sensors.php?action=get_commands";
    
    http.begin(client, url);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc["success"] == true) {
            JsonArray commands = doc["commands"];
            int count = doc["count"];
            
            if (count > 0) {
                Serial.printf("[CMD] %d comando(s) pendente(s)\n", count);
                
                for (JsonObject cmd : commands) {
                    int cmdId = cmd["id"];
                    const char* cmdName = cmd["command"];
                    
                    Serial.printf("[CMD] Executando: %s (ID: %d)\n", cmdName, cmdId);
                    
                    if (strcmp(cmdName, "CLEAR_EEPROM") == 0) {
                        markCommandExecuted(cmdId);
                        
                        Serial.println(F("[CMD] Iniciando limpeza do Preferences..."));
                        delay(1000);
                        
                        // ✅ MIGRADO: Chama função de limpar Preferences
                        clearAllPreferencesUtil();
                    }
                }
            }
        }
    } else if (httpCode != HTTP_CODE_NOT_FOUND) {
        static unsigned long lastError = 0;
        if (millis() - lastError >= 60000) {
            Serial.printf("[CMD] Erro HTTP: %d\n", httpCode);
            lastError = millis();
        }
    }
    
    http.end();
}