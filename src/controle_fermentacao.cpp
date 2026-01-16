// controle_fermentacao.cpp - Reescrito para integração BrewPi
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <cstring>
#include <time.h>

#include "definitions.h"
#include "estruturas.h"
#include "globais.h"
#include "http_client.h"
#include "BrewPiStructs.h"
#include "BrewPiTempControl.h"
#include "controle_fermentacao.h"
#include "eeprom_layout.h"
#include "fermentacao_stages.h"
#include "gerenciador_sensores.h"

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
// CONTROLE DE TEMPERATURA - INTEGRAÇÃO BREWPI
// =====================================================

void updateTargetTemperature(float newTemp) {
    // Converte float para fixed-point BrewPi
    temperature temp = floatToTemp(newTemp);
    brewPiControl.setBeerTemp(temp);
    
    // Atualiza estado global (compatibilidade)
    fermentacaoState.tempTarget = newTemp;
    state.targetTemp = newTemp;
    
    Serial.printf("[BrewPi] 🎯 Novo alvo: %.2f°C\n", newTemp);
}

float getCurrentBeerTemp() {
    temperature temp = brewPiControl.getBeerTemp();
    float tempFloat = tempToFloat(temp);
    
    // Atualiza estado global (compatibilidade)
    state.currentTemp = tempFloat;
    
    return tempFloat;
}

void resetPIDState() {
    brewPiControl.reset();
    Serial.println(F("[BrewPi] ✅ Estado do controle resetado"));
}

// =====================================================
// CONTROLE DE ESTADO
// =====================================================

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

    // Reset do controle BrewPi
    brewPiControl.reset();
    
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
    
    Serial.println(F("[BrewPi] ✅ Sistema resetado na desativação"));
}

void setupActiveListener() {
    Serial.println(F("[MySQL] Sistema inicializado"));
    loadStateFromEEPROM();
    
    // Reset do controle BrewPi na inicialização
    brewPiControl.reset();
    Serial.println(F("[BrewPi] ✅ Sistema resetado na inicialização"));
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

            // Reset completo do BrewPi
            brewPiControl.reset();
            Serial.println(F("[BrewPi] ✅ Sistema resetado para nova fermentação"));
            
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
                Serial.printf("  → Etapa mudou EXTERNAMENTE: %d -> %d\n", 
                            fermentacaoState.currentStageIndex, currentStageIndex);
                
                fermentacaoState.currentStageIndex = currentStageIndex;
                stageStarted = false;
                fermentacaoState.stageStartEpoch = 0;
                fermentacaoState.targetReachedSent = false;
                        
                brewPiControl.reset();
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
            brewPiControl.reset();
            Serial.println(F("[BrewPi] ✅ Sistema resetado em modo standby"));
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
// ✅ TROCA DE FASE
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    
    // Debug periódico
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 30000) {
        lastDebug = millis();
        Serial.println(F("\n╔════════════════════════════════════╗"));
        Serial.println(F("║   DEBUG verificarTrocaDeFase()     ║"));
        Serial.println(F("╠════════════════════════════════════╣"));
        Serial.printf("║ stageStarted:     %s               ║\n", 
                     stageStarted ? "TRUE " : "FALSE");
        Serial.printf("║ PID atual:        %6.1f°C          ║\n", 
                     fermentacaoState.tempTarget);
        Serial.printf("║ Alvo etapa:       %6.1f°C          ║\n", 
                     fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp);
        Serial.println(F("╚════════════════════════════════════╝\n"));
    }
    
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
        stageStarted = true;
        fermentacaoState.targetReachedSent = false;
        fermentacaoState.stageStartEpoch = 0;
        
        // Reset do BrewPi para nova etapa
        brewPiControl.reset();
        Serial.println(F("[BrewPi] ✅ Sistema resetado para nova etapa"));
        
        // Determina temperatura alvo
        float newTargetTemp;
        if (stage.type == STAGE_RAMP) {
            newTargetTemp = stage.startTemp;
        } else {
            newTargetTemp = stage.targetTemp;
        }
        
        // Define temperatura no BrewPi
        updateTargetTemperature(newTargetTemp);
        
        saveStateToEEPROM();
        
        Serial.printf("[Fase] ▶️  Etapa %d/%d iniciada - Alvo: %.1f°C (tipo: ", 
                    fermentacaoState.currentStageIndex + 1,
                    fermentacaoState.totalStages,
                    newTargetTemp);
                    
        switch (stage.type) {
            case STAGE_TEMPERATURE:
                Serial.println("TEMPERATURE)");
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
        // ✅ CORREÇÃO: Atualiza timestamp ANTES de verificar
        state.lastTempUpdate = millis();
        
        // Usa temperatura lida do BrewPi
        float currentTemp = getCurrentBeerTemp();
        float stageTargetTemp = stage.targetTemp;
        float diff = abs(currentTemp - stageTargetTemp);
        targetReached = (diff <= TEMPERATURE_TOLERANCE);
        
        // Debug periódico
        static unsigned long lastDebug2 = 0;
        unsigned long now = millis();
        if (now - lastDebug2 > 60000 && !fermentacaoState.targetReachedSent) {
            lastDebug2 = now;
            Serial.printf("[Fase] Aguardando alvo: Temp=%.1f°C, Alvo=%.1f°C, Diff=%.1f°C, Atingiu=%s\n",
                         currentTemp, stageTargetTemp, diff, targetReached ? "SIM" : "NÃO");
        }
        
        if (targetReached && !fermentacaoState.targetReachedSent) {
            fermentacaoState.targetReachedSent = true;
            
            if (fermentacaoState.stageStartEpoch == 0) {
                fermentacaoState.stageStartEpoch = nowEpoch;
                saveStateToEEPROM();
                Serial.printf("[Fase] 🎯 Temperatura FINAL da etapa atingida: %.1f°C!\n", stageTargetTemp);
                Serial.printf("[Fase] ⏱️  Contagem iniciada em: %s\n", formatTime(nowEpoch).c_str());
            }
        }
    } 
    else if (stage.type == STAGE_RAMP) {
        targetReached = true;
        
        if (fermentacaoState.stageStartEpoch == 0) {
            fermentacaoState.stageStartEpoch = nowEpoch;
            saveStateToEEPROM();
            Serial.println(F("[Fase] ⏱️  Contagem de rampa iniciada"));
        }
    }

    // =====================================================
    // ✅ CÁLCULO DO TEMPO DECORRIDO
    // =====================================================
    float elapsedH = 0;
    
    if (fermentacaoState.stageStartEpoch > 0) {
        elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
        if (elapsedH < 0) elapsedH = 0;
    }
    
    // Debug periódico
    static unsigned long lastDebug3 = 0;
    if (millis() - lastDebug3 > 300000) {
        lastDebug3 = millis();
        
        Serial.printf("[Fase] Etapa %d/%d: ", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages);
        
        if (fermentacaoState.stageStartEpoch > 0) {
            Serial.printf("%.1fh/", elapsedH);
            
            switch (stage.type) {
                case STAGE_TEMPERATURE:
                case STAGE_GRAVITY:
                case STAGE_GRAVITY_TIME:
                    Serial.printf("%.1fh decorridas", (float)stage.holdTimeHours);
                    break;
                case STAGE_RAMP:
                    Serial.printf("%.1fh decorridas", (float)stage.rampTimeHours);
                    break;
            }
        } else {
            Serial.print("Aguardando temperatura alvo");
        }
        
        Serial.printf(" (targetReached: %s)\n", targetReached ? "SIM" : "NÃO");
    }

    // =====================================================
    // CONTROLE DE RAMPA
    // =====================================================
    if (stage.type == STAGE_RAMP) {
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
            if (targetReached && fermentacaoState.stageStartEpoch > 0) {
                if (elapsedH >= stage.holdTimeHours) {
                    stageCompleted = true;
                }
            }
            break;

        case STAGE_RAMP:
            if (fermentacaoState.stageStartEpoch > 0 && 
                elapsedH >= stage.rampTimeHours) {
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
                bool timeoutReached = (fermentacaoState.stageStartEpoch > 0 && 
                                      elapsedH >= stage.maxTimeHours);
                if (mySpindel.gravity <= stage.targetGravity || timeoutReached) {
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
      
        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        fermentacaoState.stageStartEpoch = 0;
        fermentacaoState.targetReachedSent = false;

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
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

// =====================================================
// STATUS DETALHADO - USANDO BREWPI
// =====================================================

DetailedControlStatus getDetailedStatus() {
    return brewPiControl.getDetailedStatus();
}

void verificarTargetAtingido() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    float currentTemp = getCurrentBeerTemp();
    float diff = abs(currentTemp - fermentacaoState.tempTarget);

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
// ✅ ENVIAR ESTADO COMPLETO
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

    // Temperatura alvo da ETAPA vs temperatura PID
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        doc["stageTargetTemp"] = stage.targetTemp;
    }

    doc["pidTargetTemp"] = fermentacaoState.tempTarget;
    doc["currentTargetTemp"] = fermentacaoState.tempTarget;
    doc["targetReached"] = fermentacaoState.targetReachedSent;
    
    // Cálculo do tempo restante
    if (fermentacaoState.stageStartEpoch > 0 && 
        fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        
        time_t nowEpoch = getCurrentEpoch();
        
        if (nowEpoch > 0) {
            FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            
            if (stage.type == STAGE_TEMPERATURE) {
                if (fermentacaoState.targetReachedSent) {
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
                    JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                    timeRemaining["value"] = stage.durationDays;
                    timeRemaining["unit"] = "days";
                    timeRemaining["status"] = "waiting";
                }
            }
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
            else if (stage.type == STAGE_GRAVITY) {
                JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
                timeRemaining["value"] = 0;
                timeRemaining["unit"] = "indefinite";
                timeRemaining["status"] = "waiting_gravity";
            }
            
            if (stage.type == STAGE_RAMP) {
                float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0;
                float progress = elapsedH / stage.rampTimeHours;
                if (progress < 0) progress = 0;
                if (progress > 1) progress = 1;
                
                doc["rampProgress"] = progress * 100.0;
            }
        }
    }
    
    // Status detalhado do BrewPi
    DetailedControlStatus detailedStatus = brewPiControl.getDetailedStatus();
    
    doc["cooling"] = detailedStatus.coolerActive;
    doc["heating"] = detailedStatus.heaterActive;
    
    JsonObject controlStatus = doc["control_status"].to<JsonObject>();
    controlStatus["state"] = detailedStatus.stateName;
    controlStatus["is_waiting"] = detailedStatus.isWaiting;
    
    if (detailedStatus.isWaiting) {
        if (detailedStatus.waitTimeRemaining > 0) {
            controlStatus["wait_seconds"] = detailedStatus.waitTimeRemaining;
            controlStatus["wait_reason"] = detailedStatus.waitReason;
            
            String waitDisplay;
            if (detailedStatus.waitTimeRemaining < 60) {
                waitDisplay = String(detailedStatus.waitTimeRemaining) + "s";
            } else if (detailedStatus.waitTimeRemaining < 3600) {
                uint16_t min = detailedStatus.waitTimeRemaining / 60;
                uint16_t sec = detailedStatus.waitTimeRemaining % 60;
                waitDisplay = String(min) + "m";
                if (sec > 0) waitDisplay += String(sec) + "s";
            } else {
                uint16_t hours = detailedStatus.waitTimeRemaining / 3600;
                uint16_t min = (detailedStatus.waitTimeRemaining % 3600) / 60;
                waitDisplay = String(hours) + "h";
                if (min > 0) waitDisplay += String(min) + "m";
            }
            controlStatus["wait_display"] = waitDisplay;
        } else {
            controlStatus["wait_reason"] = detailedStatus.waitReason;
            controlStatus["wait_display"] = "aguardando";
        }
    }
    
    if (detailedStatus.peakDetection) {
        controlStatus["peak_detection"] = true;
        controlStatus["estimated_peak"] = detailedStatus.estimatedPeak;
    }
    
    // ✅ CORREÇÃO: Usa timestamp Unix real (não uptime)
    time_t nowEpoch = getCurrentEpoch();
    if (nowEpoch > 0) {
        doc["timestamp"] = nowEpoch;  // Unix timestamp em segundos
    }
    doc["uptime_ms"] = millis();  // Uptime separado para debug
    
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