// http_commands.h - Gerenciamento de comandos HTTP do servidor
#pragma once
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "http_client.h"
#include "eeprom_utils.h"

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
    
    // Prepara JSON
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
        return;  // Silencioso quando offline
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
                
                // Processa cada comando
                for (JsonObject cmd : commands) {
                    int cmdId = cmd["id"];
                    const char* cmdName = cmd["command"];
                    
                    Serial.printf("[CMD] Executando: %s (ID: %d)\n", cmdName, cmdId);
                    
                    // Executa o comando
                    if (strcmp(cmdName, "CLEAR_EEPROM") == 0) {
                        // Marca como executado ANTES de limpar (para não perder a confirmação)
                        markCommandExecuted(cmdId);
                        
                        Serial.println(F("[CMD] Iniciando limpeza da EEPROM..."));
                        delay(1000); // Aguarda confirmação chegar ao servidor
                        
                        // Chama sua função existente (que já reinicia o ESP)
                        clearAllEEPROM();
                        
                        // Nota: clearAllEEPROM() reinicia o ESP, então código após não executa
                    }
                }
            }
        }
    } else if (httpCode != HTTP_CODE_NOT_FOUND) {
        // Só loga erro se não for 404 (que é esperado quando não há comandos)
        static unsigned long lastError = 0;
        if (millis() - lastError >= 60000) {
            Serial.printf("[CMD] Erro HTTP: %d\n", httpCode);
            lastError = millis();
        }
    }
    
    http.end();
}
