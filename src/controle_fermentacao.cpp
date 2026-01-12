// controle_fermentacao.cpp - VERSÃO CORRIGIDA
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <cstring>
#include <time.h>

#include "definitions.h"
#include "estruturas.h"
#include "globais.h"
#include "http_client.h"
#include "controle_fermentacao.h"
#include "eeprom_layout.h"
#include "fermentacao_stages.h"
#include "gerenciador_sensores.h"
#include "controle_temperatura.h"
#include "rampa_suave.h"

extern FermentadorHTTPClient httpClient;

// =====================================================
// VARIÁVEIS DE CONTROLE
// =====================================================
unsigned long lastActiveCheck = 0;
char lastActiveId[64] = "";
bool isFirstCheck = true;
bool stageStarted = false;

// =====================================================
// FUNÇÕES AUXILIARES LOCAIS
// =====================================================

static void safe_strcpy(char* dest, const char* src, size_t destSize) {
    if (!dest || destSize == 0) return;
    
    if (src) {
        strncpy(dest, src, destSize - 1);
        dest[destSize - 1] = '\0';
    } else {
        dest[0] = '\0';
    }
}

bool isValidString(const char* str) {
    return str && str[0] != '\0';
}

// =====================================================
// FUNÇÕES DE TEMPO
// =====================================================

String formatTime(time_t timestamp) {
    if (timestamp == 0) return "INVALID";
    
    struct tm timeinfo;
    gmtime_r(&timestamp, &timeinfo);
    
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S UTC", &timeinfo);
    return String(buffer);
}

time_t getCurrentEpoch() {
    static time_t lastValidEpoch = 0;
    static unsigned long lastValidMillis = 0;
    static bool epochInitialized = false;
    
    time_t now = time(nullptr);
    
    if (now < 1577836800L) {
        if (!epochInitialized) {
            EEPROM.begin(EEPROM_SIZE);
            EEPROM.get(ADDR_LAST_VALID_EPOCH, lastValidEpoch);
            EEPROM.get(ADDR_LAST_VALID_MILLIS, lastValidMillis);
            epochInitialized = true;
            
            if (lastValidEpoch > 1577836800L) {
                Serial.print(F("[NTP] ⚠️  Usando backup EEPROM: "));
                Serial.println(formatTime(lastValidEpoch));
            }
        }
        
        if (lastValidEpoch > 1577836800L) {
            return lastValidEpoch + ((millis() - lastValidMillis) / 1000);
        }
        
        Serial.println(F("[NTP] ⚠️  Relógio não sincronizado!"));
        return 0;
    }
    
    static unsigned long lastBackup = 0;
    if (millis() - lastBackup > 60000) {
        lastValidEpoch = now;
        lastValidMillis = millis();
        
        EEPROM.begin(EEPROM_SIZE);
        EEPROM.put(ADDR_LAST_VALID_EPOCH, lastValidEpoch);
        EEPROM.put(ADDR_LAST_VALID_MILLIS, lastValidMillis);
        EEPROM.commit();
        lastBackup = millis();
    }
    
    return now;
}

// =====================================================
// EEPROM
// =====================================================
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    eepromWriteString(fermentacaoState.activeId, ADDR_ACTIVE_ID, sizeof(fermentacaoState.activeId));
    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    time_t epoch = fermentacaoState.stageStartEpoch;
    EEPROM.put(ADDR_STAGE_START_TIME, epoch);

    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);
    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    
    if (!EEPROM.commit()) {
        Serial.println(F("[EEPROM] ❌ Falha ao salvar estado"));
    } else {
        Serial.print(F("[EEPROM] ✅ Estado salvo (início: "));
        Serial.print(formatTime(epoch));
        Serial.println(")");
    }
}

void loadStateFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    if (EEPROM.read(ADDR_CONFIG_SAVED) != 1) {
        Serial.println(F("[EEPROM] Nenhum estado salvo"));
        return;
    }

    eepromReadString(fermentacaoState.activeId, 
                     sizeof(fermentacaoState.activeId), 
                     ADDR_ACTIVE_ID, 
                     sizeof(fermentacaoState.activeId));

    if (!isValidString(fermentacaoState.activeId)) {
        Serial.println(F("[EEPROM] ⚠️  ID inválido, limpando..."));
        clearEEPROM();
        fermentacaoState.clear();
        return;
    }

    EEPROM.get(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    time_t savedEpoch;
    EEPROM.get(ADDR_STAGE_START_TIME, savedEpoch);
    fermentacaoState.stageStartEpoch = savedEpoch;

    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);

    fermentacaoState.active = isValidString(fermentacaoState.activeId);

    if (fermentacaoState.active && !isValidString(fermentacaoState.activeId)) {
        Serial.println(F("[EEPROM] ⚠️  Estado inconsistente, limpando..."));
        clearEEPROM();
        fermentacaoState.clear();
        fermentacaoState.tempTarget = 20.0;
        state.targetTemp = 20.0;
        return;
    }

    Serial.print(F("[EEPROM] ✅ Estado restaurado: ID="));
    Serial.print(fermentacaoState.activeId);
    Serial.print(", início=");
    Serial.println(formatTime(savedEpoch));
}

void clearEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    for (int i = ADDR_FERMENTATION_START; i <= 127; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        Serial.println(F("[EEPROM] ✅ Seção de fermentação limpa"));
    } else {
        Serial.println(F("[EEPROM] ❌ Falha ao limpar EEPROM"));
    }
}

// =====================================================
// CONTROLE DE ESTADO
// =====================================================
void updateTargetTemperature(float temp) {
    if (temp < MIN_SAFE_TEMPERATURE) {
        temp = MIN_SAFE_TEMPERATURE;
        Serial.printf("[Segurança] ⚠️ Temperatura limitada para mínimo seguro: %.1f°C\n", temp);
    }
    if (temp > MAX_SAFE_TEMPERATURE) {
        temp = MAX_SAFE_TEMPERATURE;
        Serial.printf("[Segurança] ⚠️ Temperatura limitada para máximo seguro: %.1f°C\n", temp);
    }
    
    fermentacaoState.tempTarget = temp;
    state.targetTemp = temp;
}

void concluirFermentacaoMantendoTemperatura() {
    Serial.println(F("[Fase] ✅ Fermentação concluída - mantendo temperatura atual"));
    
    JsonDocument doc;
    doc["status"] = "completed";
    time_t completionEpoch = getCurrentEpoch();
    if (completionEpoch > 0) {
        doc["completedAt"] = completionEpoch;
    }
    doc["message"] = "Fermentação concluída automaticamente - mantendo temperatura";
    
    String payload;
    serializeJson(doc, payload);
    
    if (httpClient.isConnected()) {
        httpClient.updateFermentationState(fermentacaoState.activeId, payload);
    }
    
    fermentacaoState.concluidaMantendoTemp = true;
    
    Serial.println(F("[Fase] 🌡️  Sistema mantém temperatura atual até comando manual"));
    Serial.printf("[Fase] 🔒 Temperatura mantida: %.1f°C\n", fermentacaoState.tempTarget);
}

void deactivateCurrentFermentation() {
    Serial.println(F("[MySQL] 🧹 Desativando fermentação"));

    resetPIDState();
    
    fermentacaoState.activeId[0] = '\0';
    lastActiveId[0] = '\0';

    fermentacaoState.active = false;
    fermentacaoState.concluidaMantendoTemp = false;
    fermentacaoState.currentStageIndex = 0;
    fermentacaoState.totalStages = 0;
    fermentacaoState.stageStartEpoch = 0;
    fermentacaoState.targetReachedSent = false;
    stageStarted = false;

    updateTargetTemperature(DEFAULT_TEMPERATURE);
    clearEEPROM();
    saveStateToEEPROM();
    
    Serial.println(F("[PID] ✅ Estado do PID resetado na desativação"));
}

void setupActiveListener() {
    Serial.println(F("[MySQL] Sistema inicializado"));
    loadStateFromEEPROM();
    
    resetPIDState();
    Serial.println(F("[PID] ✅ Estado do PID resetado na inicialização do sistema"));
}

// =====================================================
// VERIFICAÇÃO DE COMANDOS DO SITE
// =====================================================
void checkPauseOrComplete() {
    if (!fermentacaoState.active) return;
    if (!httpClient.isConnected()) return;
    
    JsonDocument doc;
    
    if (!httpClient.getConfiguration(fermentacaoState.activeId, doc)) {
        return;
    }
    
    const char* status = doc["status"] | "active";
    
    if (strcmp(status, "paused") == 0) {
        Serial.println(F("[MySQL] ⏸️  Fermentação PAUSADA pelo site"));
        deactivateCurrentFermentation();
    } else if (strcmp(status, "completed") == 0) {
        Serial.println(F("[MySQL] ✅ Fermentação CONCLUÍDA pelo site"));
        
        if (fermentacaoState.concluidaMantendoTemp) {
            Serial.println(F("[MySQL] 🧹 Finalizando manutenção de temperatura por comando do site"));
            deactivateCurrentFermentation();
        } else {
            concluirFermentacaoMantendoTemperatura();
        }
    }
}

void getTargetFermentacao() {
    unsigned long now = millis();

    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[MySQL] ⚠️ WiFi desconectado"));
        isFirstCheck = false;
        return;
    }

    Serial.println(F("\n========================================"));
    Serial.println(F("[MySQL] 🔍 INICIANDO BUSCA DE FERMENTAÇÃO"));
    Serial.println(F("========================================"));

    JsonDocument doc;
    
    bool requestOk = httpClient.getActiveFermentation(doc);
    
    Serial.printf("[MySQL] getActiveFermentation() retornou: %s\n", 
                  requestOk ? "TRUE" : "FALSE");
    
    if (!requestOk) {
        Serial.println(F("[MySQL] ❌ Falha na requisição HTTP"));
        isFirstCheck = false;
        return;
    }

    Serial.println(F("\n[MySQL] 📄 DOCUMENTO JSON RECEBIDO:"));
    serializeJsonPretty(doc, Serial);
    Serial.println();

    bool active = doc["active"] | false;
    
    String idString;
    if (doc["id"].is<int>()) {
        idString = String(doc["id"].as<int>());
    } else if (doc["id"].is<const char*>()) {
        idString = doc["id"].as<const char*>();
    }
    
    const char* id = idString.c_str();
    const char* name = doc["name"] | "";
    const char* status = doc["status"] | "";
    int currentStageIndex = doc["currentStageIndex"] | 0;
    
    Serial.println(F("\n[MySQL] 🔍 VALORES EXTRAÍDOS:"));
    Serial.printf("  active: %s\n", active ? "TRUE" : "FALSE");
    Serial.printf("  id: '%s' (length: %d)\n", id, strlen(id));
    Serial.printf("  name: '%s'\n", name);
    Serial.printf("  status: '%s'\n", status);
    Serial.printf("  currentStageIndex: %d\n", currentStageIndex);
    
    Serial.println(F("\n[MySQL] 🔍 ESTADO ATUAL DO SISTEMA:"));
    Serial.printf("  fermentacaoState.active: %s\n", 
                  fermentacaoState.active ? "TRUE" : "FALSE");
    Serial.printf("  fermentacaoState.activeId: '%s'\n", 
                  fermentacaoState.activeId);
    Serial.printf("  lastActiveId: '%s'\n", lastActiveId);

    if (!isValidString(id)) {
        Serial.println(F("[MySQL] ⚠️ ID é inválido ou vazio!"));
        id = "";
    } else {
        Serial.printf("[MySQL] ✅ ID válido: '%s'\n", id);
    }

    Serial.println(F("\n[MySQL] 🔍 DECISÃO:"));

    if (active && isValidString(id)) {
        Serial.println(F("  → Fermentação ATIVA detectada no servidor"));
        
        if (strcmp(id, lastActiveId) != 0) {
            Serial.println(F("  → ID DIFERENTE do último conhecido"));
            Serial.printf("     Anterior: '%s'\n", lastActiveId);
            Serial.printf("     Novo:     '%s'\n", id);
            Serial.println(F("  → INICIANDO NOVA FERMENTAÇÃO"));

            resetPIDState();
            Serial.println(F("[PID] ✅ Estado do PID resetado para nova fermentação"));
            
            fermentacaoState.active = true;
            fermentacaoState.concluidaMantendoTemp = false;
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            fermentacaoState.currentStageIndex = currentStageIndex;
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            Serial.printf("[MySQL] 🔧 Carregando configuração ID: %s\n", id);
            loadConfigParameters(id);

            stageStarted = false;
            fermentacaoState.targetReachedSent = false;
            fermentacaoState.stageStartEpoch = 0;

            saveStateToEEPROM();
            
            Serial.println(F("[MySQL] ✅ CONFIGURAÇÃO CONCLUÍDA"));
            Serial.printf("  activeId: '%s'\n", fermentacaoState.activeId);
            Serial.printf("  tempTarget: %.1f°C\n", fermentacaoState.tempTarget);
            Serial.printf("  totalStages: %d\n", fermentacaoState.totalStages);
        } else {
            Serial.println(F("  → MESMO ID do último conhecido"));
            Serial.println(F("  → Fermentação já configurada"));
            
            if (currentStageIndex != fermentacaoState.currentStageIndex) {
                Serial.printf("  → Etapa mudou: %d -> %d\n", 
                            fermentacaoState.currentStageIndex, currentStageIndex);
                fermentacaoState.currentStageIndex = currentStageIndex;
                stageStarted = false;
                fermentacaoState.stageStartEpoch = 0;
                
                resetPIDState();
                Serial.println(F("[PID] ✅ Estado do PID resetado para mudança de etapa externa"));
                
                saveStateToEEPROM();
            }
        }
    } else if (fermentacaoState.active && !active) {
        if (fermentacaoState.concluidaMantendoTemp) {
            Serial.println(F("  → Concluída localmente, mantendo temperatura (servidor offline)"));
        } else {
            Serial.println(F("  → Fermentação estava ativa LOCALMENTE"));
            Serial.println(F("  → Servidor indica NÃO ATIVA"));
            Serial.println(F("  → DESATIVANDO"));
            deactivateCurrentFermentation();
        }
    } else if (!active && !fermentacaoState.active) {
        Serial.println(F("  → Nenhuma fermentação ativa"));
        Serial.println(F("  → Sistema em STANDBY"));
        
        if (state.targetTemp == DEFAULT_TEMPERATURE) {
            resetPIDState();
            Serial.println(F("[PID] ✅ Estado do PID resetado em modo standby"));
        }
    } else if (!active && fermentacaoState.active) {
        Serial.println(F("  → Servidor offline mas temos estado local"));
        Serial.println(F("  → MANTENDO fermentação local"));
    }

    Serial.println(F("========================================"));
    Serial.println(F("[MySQL] FIM DA VERIFICAÇÃO"));
    Serial.println(F("========================================\n"));

    isFirstCheck = false;
}

// =====================================================
// CONFIGURAÇÃO DE ETAPAS
// =====================================================
void loadConfigParameters(const char* configId) {
    if (!configId || strlen(configId) == 0) {
        Serial.println(F("[MySQL] ❌ ID inválido"));
        return;
    }

    Serial.printf("[MySQL] 🔧 Buscando config: %s\n", configId);
    
    JsonDocument doc;
    
    if (!httpClient.getConfiguration(configId, doc)) {
        Serial.println(F("[MySQL] ❌ Falha ao buscar configuração"));
        return;
    }

    fermentacaoState.currentStageIndex = doc["currentStageIndex"] | 0;
    
    const char* name = doc["name"] | "Sem nome";
    fermentacaoState.setConfigName(name);
    
    JsonArray stages = doc["stages"];
    int count = 0;
    
    for (JsonVariant stage : stages) {
        if (count >= MAX_STAGES) {
            Serial.println(F("[MySQL] ⚠️  Máximo de etapas excedido"));
            break;
        }

        FermentationStage& s = fermentacaoState.stages[count];
        
        const char* type = stage["type"] | "temperature";
        if (strcmp(type, "ramp") == 0) {
            s.type = STAGE_RAMP;
        } else if (strcmp(type, "gravity") == 0) {
            s.type = STAGE_GRAVITY;
        } else if (strcmp(type, "gravity_time") == 0) {
            s.type = STAGE_GRAVITY_TIME;
        } else {
            s.type = STAGE_TEMPERATURE;
        }

        s.targetTemp = stage["targetTemp"] | 20.0;
        s.startTemp = stage["startTemp"] | 20.0;
        s.rampTimeHours = stage["rampTime"] | 0;
        s.durationDays = stage["duration"] | 0;
        s.targetGravity = stage["targetGravity"] | 0.0;
        s.timeoutDays = stage["timeoutDays"] | 0;
        
        s.holdTimeHours = s.durationDays * 24;
        s.maxTimeHours = s.timeoutDays * 24;
        
        s.startTime = 0;
        s.completed = false;

        count++;
    }

    fermentacaoState.totalStages = count;

    if (count > 0 && fermentacaoState.currentStageIndex < count) {
        float targetTemp = fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp;
        updateTargetTemperature(targetTemp);
        Serial.printf("[MySQL] 🌡️  Temperatura alvo: %.1f°C\n", targetTemp);
    }

    Serial.printf("[MySQL] ✅ Configuração carregada: %d etapas\n", count);
}

// =====================================================
// ✅ CORREÇÃO PRINCIPAL: TROCA DE FASE
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    
    updateSmoothRamp();
    
    if (fermentacaoState.totalStages == 0) {
        Serial.println(F("[Fase] ⚠️  0 etapas, desativando..."));
        deactivateCurrentFermentation();
        return;
    }
    
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        Serial.println(F("[Fase] ⚠️  Índice inválido"));
        
        if (fermentacaoState.totalStages > 0) {
            fermentacaoState.currentStageIndex = 0;
            Serial.println(F("[Fase] 🔄 Recomeçando da etapa 0"));
        } else {
            deactivateCurrentFermentation();
        }
        return;
    }

    FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
    
    time_t nowEpoch = getCurrentEpoch();
    
    if (nowEpoch == 0) {
        Serial.println(F("[Fase] ⚠️ Aguardando sincronização NTP..."));
        return;
    }
    
    // =====================================================
    // ✅ INÍCIO DE NOVA ETAPA
    // =====================================================
    if (!stageStarted) {
        // Marca como iniciada MAS não define stageStartEpoch ainda
        // (será definido quando temperatura for atingida para etapas TEMPERATURE)
        stageStarted = true;
        fermentacaoState.targetReachedSent = false;
        
        resetPIDState();
        Serial.println(F("[PID] ✅ Estado do PID resetado para nova etapa"));
        
        float newTargetTemp;
        if (stage.type == STAGE_RAMP) {
            newTargetTemp = stage.startTemp;
        } else {
            newTargetTemp = stage.targetTemp;
        }
        
        float currentTemp = state.currentTemp;
        float tempDiff = fabs(newTargetTemp - currentTemp);
        
        if (tempDiff > RAMP_THRESHOLD && tempDiff > 0.1f) {
            Serial.printf("[Fase] 🔄 Mudança grande na INICIALIZAÇÃO: %.1f°C -> %.1f°C (Δ=%.1f°C)\n",
                         currentTemp, newTargetTemp, tempDiff);
            setupSmoothRamp(currentTemp, newTargetTemp);
        } else {
            updateTargetTemperature(newTargetTemp);
            Serial.printf("[Fase] 🌡️  Temperatura alvo definida: %.1f°C\n", newTargetTemp);
        }
        
        // ✅ IMPORTANTE: Para etapas TEMPERATURE, NÃO define stageStartEpoch ainda
        // Será definido quando temperatura for atingida
        if (stage.type != STAGE_TEMPERATURE) {
            fermentacaoState.stageStartEpoch = nowEpoch;
        } else {
            fermentacaoState.stageStartEpoch = 0; // Zero indica "aguardando temperatura"
        }
        
        saveStateToEEPROM();
        
        Serial.printf("[Fase] ▶️  Etapa %d/%d iniciada em %s (tipo: ", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages,
                     formatTime(nowEpoch).c_str());
                     
        switch (stage.type) {
            case STAGE_TEMPERATURE:
                Serial.println("TEMPERATURE - aguardando temperatura alvo)");
                break;
            case STAGE_RAMP:
                Serial.println("RAMP)");
                break;
            case STAGE_GRAVITY:
                Serial.println("GRAVITY)");
                break;
            case STAGE_GRAVITY_TIME:
                Serial.println("GRAVITY_TIME)");
                break;
        }
    }

    // =====================================================
    // ✅ VERIFICAÇÃO DE TEMPERATURA ALVO ATINGIDA
    // =====================================================
    bool targetReached = false;
    bool needsTemperature = (stage.type == STAGE_TEMPERATURE || 
                            stage.type == STAGE_GRAVITY || 
                            stage.type == STAGE_GRAVITY_TIME);
    
    if (needsTemperature) {
        float diff = abs(state.currentTemp - fermentacaoState.tempTarget);
        targetReached = (diff <= TEMPERATURE_TOLERANCE);
        
        if (targetReached && !fermentacaoState.targetReachedSent) {
            fermentacaoState.targetReachedSent = true;
            
            // ✅ CORREÇÃO CRÍTICA: Para etapas TEMPERATURE, agora SIM inicia a contagem
            if (stage.type == STAGE_TEMPERATURE) {
                fermentacaoState.stageStartEpoch = nowEpoch;
                Serial.println(F("[Fase] 🎯 Temperatura alvo atingida! INICIANDO CONTAGEM DE TEMPO"));
                Serial.printf("[Fase] ⏱️  Contagem iniciada em: %s\n", formatTime(nowEpoch).c_str());
            } else {
                // Para GRAVITY e GRAVITY_TIME, também reinicia contagem
                fermentacaoState.stageStartEpoch = nowEpoch;
                Serial.println(F("[Fase] 🎯 Temperatura alvo atingida, iniciando contagem"));
            }
            
            saveStateToEEPROM();
        }
    } else {
        targetReached = true;
    }

    // =====================================================
    // ✅ CÁLCULO DO TEMPO DECORRIDO (CORRIGIDO)
    // =====================================================
    float elapsedH = 0;
    
    if (stage.type == STAGE_TEMPERATURE) {
        // Para etapas TEMPERATURE, só calcula tempo SE já atingiu temperatura
        if (fermentacaoState.stageStartEpoch > 0) {
            elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
        } else {
            elapsedH = 0; // Ainda não iniciou contagem
        }
    } else {
        // Para outras etapas, usa stageStartEpoch normalmente
        if (fermentacaoState.stageStartEpoch > 0) {
            elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
        }
    }
    
    // Debug periódico
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 300000) {
        lastDebug = millis();
        Serial.printf("[Fase] Etapa %d: %.1fh/%.1fh decorridas (targetReached: %s)\n", 
                     fermentacaoState.currentStageIndex + 1,
                     elapsedH,
                     (float)stage.holdTimeHours,
                     targetReached ? "SIM" : "NÃO");
    }

    // =====================================================
    // CONTROLE DE RAMPA
    // =====================================================
    if (stage.type == STAGE_RAMP && !isSmoothRampActive()) {
        float progress = elapsedH / stage.rampTimeHours;
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;

        float temp = stage.startTemp + (stage.targetTemp - stage.startTemp) * progress;
        updateTargetTemperature(temp);
        
        static unsigned long lastRampDebug = 0;
        if (millis() - lastRampDebug > 60000) {
            lastRampDebug = millis();
            Serial.printf("[Rampa Etapa] Progresso: %.1f°C (%.0f%%)\n", 
                         temp, progress * 100.0f);
        }
    }

    // =====================================================
    // VERIFICAÇÃO DE CONCLUSÃO DA ETAPA
    // =====================================================
    bool stageCompleted = false;

    switch (stage.type) {
        case STAGE_TEMPERATURE:
            // ✅ CORREÇÃO: Só considera concluída se:
            // 1. Temperatura foi atingida (targetReached)
            // 2. Tempo de hold passou (elapsedH >= holdTimeHours)
            if (targetReached && fermentacaoState.stageStartEpoch > 0 && 
                elapsedH >= stage.holdTimeHours) {
                stageCompleted = true;
            }
            break;

        case STAGE_RAMP:
            if (elapsedH >= stage.rampTimeHours) {
                stageCompleted = true;
            }
            break;

        case STAGE_GRAVITY:
            if (targetReached && mySpindel.gravity <= stage.targetGravity) {
                stageCompleted = true;
            }
            break;

        case STAGE_GRAVITY_TIME:
            if (targetReached) {
                if (mySpindel.gravity <= stage.targetGravity) {
                    stageCompleted = true;
                } else if (elapsedH >= stage.maxTimeHours) {
                    stageCompleted = true;
                }
            }
            break;
    }

    // =====================================================
    // TRANSIÇÃO PARA PRÓXIMA ETAPA
    // =====================================================
    if (stageCompleted) {
        Serial.printf("[Fase] ✅ Etapa %d/%d concluída após %.1fh\n", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages,
                     elapsedH);
        
        float currentTemp = state.currentTemp;
        
        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        fermentacaoState.stageStartEpoch = 0;
        fermentacaoState.targetReachedSent = false;

        resetPIDState();
        Serial.println(F("[PID] ✅ Estado do PID resetado para transição de etapa"));

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            FermentationStage& next = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            
            float nextTargetTemp;
            if (next.type == STAGE_RAMP) {
                nextTargetTemp = next.startTemp;
            } else {
                nextTargetTemp = next.targetTemp;
            }
            
            float tempDiff = fabs(nextTargetTemp - currentTemp);
            
            if (tempDiff > RAMP_THRESHOLD && tempDiff > 0.1f) {
                Serial.printf("[Fase] 🔄 Mudança grande na TRANSIÇÃO: %.1f°C -> %.1f°C (Δ=%.1f°C)\n",
                             currentTemp, nextTargetTemp, tempDiff);
                setupSmoothRamp(currentTemp, nextTargetTemp);
            } else {
                updateTargetTemperature(nextTargetTemp);
                Serial.printf("[Fase] 🌡️  Nova temperatura alvo: %.1f°C\n", nextTargetTemp);
            }
            
            saveStateToEEPROM();
            
            Serial.printf("[Fase] ↪️  Indo para etapa %d/%d\n", 
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages);
        } else {
            Serial.println(F("[Fase] 🎉 TODAS AS ETAPAS CONCLUÍDAS!"));
            Serial.println(F("[Fase] 🌡️  Mantendo temperatura atual até comando manual"));
            concluirFermentacaoMantendoTemperatura();
        }
    }
}

void verificarTargetAtingido() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) {
        if (httpClient.notifyTargetReached(fermentacaoState.activeId)) {
            fermentacaoState.targetReachedSent = true;
            Serial.println(F("[MySQL] 🎯 Temperatura alvo atingida!"));
        } else {
            Serial.println(F("[MySQL] ❌ Falha ao notificar alvo"));
        }
    }
}

// =====================================================
// ✅ CORREÇÃO: ENVIAR ESTADO COMPLETO
// =====================================================
void enviarEstadoCompleto() {
    if (!fermentacaoState.active && !fermentacaoState.concluidaMantendoTemp) {
        return;
    }
    
    if (!isValidString(fermentacaoState.activeId)) {
        return;
    }
    
    static unsigned long lastStateSend = 0;
    unsigned long now = millis();
    
    if (now - lastStateSend < 30000) {
        return;
    }
    
    lastStateSend = now;
    
    JsonDocument doc;
    
    doc["config_id"] = fermentacaoState.activeId;
    
    if (fermentacaoState.concluidaMantendoTemp) {
        doc["status"] = "completed_holding_temp";
        doc["message"] = "Fermentação concluída - mantendo temperatura";
    } else {
        doc["status"] = "running";
    }
    
    doc["config_name"] = fermentacaoState.configName;
    
    doc["currentStageIndex"] = fermentacaoState.currentStageIndex;
    doc["totalStages"] = fermentacaoState.totalStages;
    doc["currentTargetTemp"] = fermentacaoState.tempTarget;
    
    doc["targetReached"] = fermentacaoState.targetReachedSent;
    
    // ✅ CORREÇÃO CRÍTICA: Cálculo do tempo restante
    if (fermentacaoState.stageStartEpoch > 0 && 
        fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        
        time_t nowEpoch = getCurrentEpoch();
        
        if (nowEpoch > 0) {
            FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            
            // ✅ Para TEMPERATURE: só calcula SE temperatura foi atingida
            if (stage.type == STAGE_TEMPERATURE) {
                if (fermentacaoState.targetReachedSent) {
                    // Temperatura atingida, calcula tempo restante
                    float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
                    
                    JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                    float remainingH = (stage.holdTimeHours - elapsedH);
                    if (remainingH < 0) remainingH = 0;
                    
                    if (remainingH < 24) {
                        timeRemaining["value"] = remainingH;
                        timeRemaining["unit"] = "hours";
                    } else {
                        timeRemaining["value"] = remainingH / 24.0;
                        timeRemaining["unit"] = "days";
                    }
                    timeRemaining["status"] = "running";
                } else {
                    // Ainda não atingiu temperatura, mostra duração total
                    JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                    timeRemaining["value"] = stage.durationDays;
                    timeRemaining["unit"] = "days";
                    timeRemaining["status"] = "waiting";
                }
            }
            // Para RAMP
            else if (stage.type == STAGE_RAMP) {
                float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
                
                JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                float remainingH = stage.rampTimeHours - elapsedH;
                if (remainingH < 0) remainingH = 0;
                
                if (remainingH < 24) {
                    timeRemaining["value"] = remainingH;
                    timeRemaining["unit"] = "hours";
                } else {
                    timeRemaining["value"] = remainingH / 24.0;
                    timeRemaining["unit"] = "days";
                }
                timeRemaining["status"] = "running";
            }
            // Para GRAVITY_TIME
            else if (stage.type == STAGE_GRAVITY_TIME) {
                if (fermentacaoState.targetReachedSent) {
                    float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
                    
                    JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                    float remainingH = (stage.maxTimeHours - elapsedH);
                    if (remainingH < 0) remainingH = 0;
                    
                    timeRemaining["value"] = remainingH / 24.0;
                    timeRemaining["unit"] = "days";
                    timeRemaining["status"] = "running";
                } else {
                    JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                    timeRemaining["value"] = stage.timeoutDays;
                    timeRemaining["unit"] = "days";
                    timeRemaining["status"] = "waiting";
                }
            }
            // Para GRAVITY
            else if (stage.type == STAGE_GRAVITY) {
                JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                timeRemaining["value"] = 0;
                timeRemaining["unit"] = "indefinite";
                timeRemaining["status"] = "waiting_gravity";
            }
            
            // Progresso da rampa (se aplicável)
            if (stage.type == STAGE_RAMP) {
                float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
                float progress = elapsedH / stage.rampTimeHours;
                if (progress < 0) progress = 0;
                if (progress > 1) progress = 1;
                
                doc["rampProgress"] = progress * 100.0;
            }
        }
    }
    
    doc["cooling"] = cooler.estado;
    doc["heating"] = heater.estado;
    
    doc["timestamp"] = millis();
    
    String payload;
    serializeJson(doc, payload);
    
    if (httpClient.updateFermentationState(fermentacaoState.activeId, payload)) {
        Serial.println(F("[Estado] ✅ Estado completo enviado ao servidor"));
    } else {
        Serial.println(F("[Estado] ⚠️ Falha ao enviar estado"));
    }
}

// =====================================================
// ENVIAR LEITURAS DOS SENSORES
// =====================================================
void enviarLeiturasSensores() {
    if (!fermentacaoState.active && !fermentacaoState.concluidaMantendoTemp) {
        return;
    }
    
    if (!isValidString(fermentacaoState.activeId)) {
        return;
    }
    
    static unsigned long lastSensorReading = 0;
    unsigned long now = millis();
    
    if (now - lastSensorReading < READINGS_UPDATE_INTERVAL) {
        return;
    }
    
    lastSensorReading = now;
    
    float tempFermenter, tempFridge;
    
    if (!readConfiguredTemperatures(tempFermenter, tempFridge)) {
        Serial.println(F("[Readings] ⚠️  Erro ao ler sensores"));
        return;
    }
    
    float gravity = mySpindel.gravity;
    
    JsonDocument doc;
    doc["config_id"] = fermentacaoState.activeId;
    doc["temp_fridge"] = tempFridge;
    doc["temp_fermenter"] = tempFermenter;
    doc["temp_target"] = fermentacaoState.tempTarget;
    
    if (gravity > 0.01) {
        doc["gravity"] = gravity;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println(F("[Readings] 📊 Enviando leitura dos sensores..."));
    
    if (httpClient.sendReading(fermentacaoState.activeId, tempFridge, 
                               tempFermenter, fermentacaoState.tempTarget, gravity)) {
        Serial.println(F("[Readings] ✅ Dados enviados para tabela 'readings'"));
        
        Serial.printf("[Readings] Fridge: %.1f°C, Fermenter: %.1f°C, Target: %.1f°C",
                     tempFridge, tempFermenter, fermentacaoState.tempTarget);
        if (gravity > 0.01) {
            Serial.printf(", Gravity: %.3f\n", gravity);
        } else {
            Serial.println();
        }
    } else {
        Serial.println(F("[Readings] ❌ Falha ao enviar dados"));
    }
}