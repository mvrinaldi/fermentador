#define ENABLE_DATABASE

#include <Arduino.h>
#include <ArduinoJson.h>
#include "firebase_conexao.h"
#include "fermentacao_firebase.h"
#include "globais.h"

// Variáveis de controle
unsigned long lastActiveCheck = 0;
const unsigned long ACTIVE_CHECK_INTERVAL = 30000;
String lastActiveId = "";
bool isFirstCheck = true;
bool listenerSetup = false;
unsigned long lastListenerCheck = 0;

// Função simplificada para atualizar temperatura alvo
void updateTargetTemperature(float temp) {
    fermentacaoState.tempTarget = temp;
    state.targetTemp = temp; // Atualiza SystemState também
}

// Desativa fermentação atual
void deactivateCurrentFermentation() {
    Serial.println(F("[Firebase] 🧹 Limpando configuração ativa..."));
    
    fermentacaoState.clear();
    lastActiveId = "";
    updateTargetTemperature(20.0); // Volta para temperatura padrão
    
    Serial.println(F("[Firebase] ✅ Configuração limpa."));
}

// Configura listener (simplificado)
void setupActiveListener() {
    if (listenerSetup) return;
    Serial.println(F("[Firebase] Monitoramento ativo"));
    listenerSetup = true;
}

// Mantém listener ativo
void keepListenerAlive() {
    unsigned long now = millis();
    if (now - lastListenerCheck >= 60000) {
        lastListenerCheck = now;
        getTargetFermentacao();
    }
}

// Para listener
void stopActiveListener() {
    listenerSetup = false;
    Serial.println(F("[Firebase] Monitoramento desabilitado."));
}

// Busca fermentação ativa (versão simplificada)
void getTargetFermentacao() {
    unsigned long now = millis();
    
    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }
    
    lastActiveCheck = now;
    Serial.println(F("[Firebase] Buscando fermentação ativa..."));

    // Versão muito simplificada - busca síncrona
    String result = Database.get<String>(aClient, "/active");
    
    if (result.length() > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, result)) {
            Serial.println(F("[Firebase] Erro ao parsear JSON."));
            return;
        }
        
        bool active = doc["active"] | false;
        String idFermentacao = doc["id"] | "";
        
        Serial.printf("[Firebase] Status: %s, ID: %s\n", 
                     active ? "ATIVA" : "INATIVA", idFermentacao.c_str());
        
        if (active && idFermentacao.length() > 0) {
            // Nova fermentação ativa
            if (idFermentacao != lastActiveId) {
                Serial.println(F("[Firebase] 🎯 Nova fermentação ativada!"));
                
                fermentacaoState.active = true;
                fermentacaoState.activeId = idFermentacao;
                strlcpy(fermentacaoState.configName, idFermentacao.c_str(), 
                       sizeof(fermentacaoState.configName));
                lastActiveId = idFermentacao;
                
                // Carrega temperatura alvo
                loadConfigParameters(idFermentacao);
            }
        } else if (fermentacaoState.active && !active) {
            // Fermentação foi desativada
            deactivateCurrentFermentation();
        } else {
            // Nenhuma fermentação ativa
            fermentacaoState.clear();
            updateTargetTemperature(20.0);
        }
    } else {
        Serial.println(F("[Firebase] Nenhuma resposta do Firebase."));
    }
    
    isFirstCheck = false;
}

// Carrega parâmetros da configuração (versão simplificada)
void loadConfigParameters(const String& configId) {
    if (configId.isEmpty()) return;
    
    String path = "/configurations/" + configId;
    Serial.printf("[Firebase] Carregando: %s\n", path.c_str());
    
    String result = Database.get<String>(aClient, path.c_str());
    
    if (result.length() > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, result)) {
            Serial.println(F("[Firebase] Erro ao parsear configuração."));
            return;
        }
        
        const char* name = doc["name"] | "Sem nome";
        int currentStageIndex = doc["currentStageIndex"] | 0;
        
        Serial.printf("[Firebase] %s (Etapa %d)\n", name, currentStageIndex + 1);
        
        // Busca etapa atual
        String stagePath = path + "/stages/" + currentStageIndex;
        String stageResult = Database.get<String>(aClient, stagePath.c_str());
        
        if (stageResult.length() > 0) {
            JsonDocument stageDoc;
            if (deserializeJson(stageDoc, stageResult)) {
                Serial.println(F("[Firebase] Erro ao parsear etapa."));
                return;
            }
            
            float targetTemp = 20.0;
            if (stageDoc["type"] == "temperature") {
                targetTemp = stageDoc["targetTemp"] | 20.0;
            } else if (stageDoc["type"] == "ramp") {
                targetTemp = stageDoc["targetTemp"] | 20.0;
            } else if (stageDoc["type"] == "gravity" || stageDoc["type"] == "gravity_time") {
                targetTemp = stageDoc["targetTemp"] | 20.0;
            }
            
            updateTargetTemperature(targetTemp);
            Serial.printf("[Firebase] ✅ Temp. alvo: %.1f°C\n", targetTemp);
        }
    }
}