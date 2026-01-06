#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <cstring>

#include "http_client.h"
#include "fermentacao_mysql.h"
#include "globais.h"
#include "eeprom_layout.h"
#include "fermentacao_stages.h"
#include "estruturas.h"

// Extern do cliente HTTP
extern FermentadorHTTPClient httpClient;

// =====================================================
// VARIÁVEIS DE CONTROLE
// =====================================================
unsigned long lastActiveCheck = 0;
char lastActiveId[64] = "";
bool isFirstCheck = true;

// Controle de fases
unsigned long stageStartTime = 0;
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

static bool isValidString(const char* str) {
    return str && str[0] != '\0';
}

// =====================================================
// EEPROM - FUNÇÕES SEGURAS
// =====================================================
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    eepromWriteString(fermentacaoState.activeId, ADDR_ACTIVE_ID, sizeof(fermentacaoState.activeId));
    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    unsigned long startMillis = (unsigned long)stageStartTime;
    EEPROM.put(ADDR_STAGE_START_TIME, startMillis);

    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);
    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    
    if (!EEPROM.commit()) {
        Serial.println(F("[EEPROM] ❌ Falha ao salvar estado"));
    } else {
        Serial.println(F("[EEPROM] ✅ Estado salvo"));
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

    unsigned long startMillis;
    EEPROM.get(ADDR_STAGE_START_TIME, startMillis);
    stageStartTime = startMillis;

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
    Serial.println(fermentacaoState.activeId);
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
    fermentacaoState.tempTarget = temp;
    state.targetTemp = temp;
}

void deactivateCurrentFermentation() {
    Serial.println(F("[MySQL] 🧹 Desativando fermentação"));

    fermentacaoState.clear();
    lastActiveId[0] = '\0';

    stageStartTime = 0;
    stageStarted = false;

    updateTargetTemperature(20.0);
    clearEEPROM();
}

void setupActiveListener() {
    Serial.println(F("[MySQL] Sistema inicializado"));
    loadStateFromEEPROM();
}

// =====================================================
// VERIFICAÇÃO DE COMANDOS DO SITE (PAUSE/COMPLETE)
// =====================================================
void checkPauseOrComplete() {
    if (!fermentacaoState.active) return;
    if (!httpClient.isConnected()) return;
    
    // Busca status atual da configuração no MySQL
    JsonDocument doc;
    
    if (!httpClient.getConfiguration(fermentacaoState.activeId, doc)) {
        return; // Falha ao buscar, ignora
    }
    
    const char* status = doc["status"] | "active";
    
    // Verifica se foi pausada ou concluída pelo site
    if (strcmp(status, "paused") == 0) {
        Serial.println(F("[MySQL] ⏸️  Fermentação PAUSADA pelo site"));
        deactivateCurrentFermentation();
    } else if (strcmp(status, "completed") == 0) {
        Serial.println(F("[MySQL] ✅ Fermentação CONCLUÍDA pelo site"));
        deactivateCurrentFermentation();
    }
}

// =====================================================
// HTTP – FERMENTAÇÃO ATIVA
// =====================================================
void getTargetFermentacao() {
    unsigned long now = millis();

    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    Serial.println(F("[MySQL] Buscando fermentação ativa"));

    JsonDocument doc;
    
    if (!httpClient.getActiveFermentation(doc)) {
        Serial.println(F("[MySQL] ❌ Falha ao buscar fermentação ativa"));
        return;
    }

    bool active = doc["active"] | false;
    const char* id = doc["id"] | "";

    if (!isValidString(id)) {
        id = "";
    }

    if (active && isValidString(id)) {
        if (strcmp(id, lastActiveId) != 0) {
            Serial.println(F("[MySQL] 🎯 Nova fermentação detectada"));

            fermentacaoState.active = true;
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            fermentacaoState.currentStageIndex = 0;
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            loadConfigParameters(id);

            stageStartTime = 0;
            stageStarted = false;
            fermentacaoState.targetReachedSent = false;

            saveStateToEEPROM();
            
            Serial.print(F("[MySQL] Nova ID configurada: "));
            Serial.println(id);
        }
    } else if (fermentacaoState.active && !active) {
        Serial.println(F("[MySQL] Fermentação desativada remotamente"));
        deactivateCurrentFermentation();
    } else if (!active && fermentacaoState.active) {
        Serial.println(F("[MySQL] Modo offline - mantendo estado local"));
    }

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

    // Processa os dados
    fermentacaoState.currentStageIndex = doc["currentStageIndex"] | 0;
    
    const char* name = doc["name"] | "Sem nome";
    fermentacaoState.setConfigName(name);
    
    // Processa as etapas
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
// TROCA DE FASE - PROCESSAMENTO LOCAL COMPLETO
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    
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
    unsigned long now = millis();

    // Inicia etapa se necessário
    if (!stageStarted) {
        stageStartTime = now;
        stageStarted = true;
        fermentacaoState.targetReachedSent = false;
        
        // Define temperatura alvo para etapa atual
        updateTargetTemperature(stage.targetTemp);
        
        saveStateToEEPROM();
        
        Serial.printf("[Fase] ▶️  Etapa %d/%d iniciada (tipo: ", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages);
                     
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

    // Calcula tempo decorrido
    float elapsedH = (now - stageStartTime) / 3600000.0;  // horas
    float elapsedD = (now - stageStartTime) / 86400000.0; // dias

    // Verifica se temperatura alvo foi atingida (para etapas que precisam)
    bool targetReached = false;
    if (stage.type == STAGE_TEMPERATURE) {
        float diff = abs(state.currentTemp - fermentacaoState.tempTarget);
        targetReached = (diff <= TEMPERATURE_TOLERANCE);
        
        // Se acabou de atingir, marca
        if (targetReached && !fermentacaoState.targetReachedSent) {
            fermentacaoState.targetReachedSent = true;
            saveStateToEEPROM();
            Serial.println(F("[Fase] 🎯 Temperatura alvo atingida, iniciando contagem"));
        }
    }

    // Processa etapa atual (retorna true quando completa)
    bool stageCompleted = processCurrentStage(stage, elapsedD, elapsedH, targetReached);

    if (stageCompleted) {
        Serial.printf("[Fase] ✅ Etapa %d/%d concluída\n", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages);
        
        // Avança para próxima etapa
        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        stageStartTime = 0;
        fermentacaoState.targetReachedSent = false;

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            // Há mais etapas
            FermentationStage& next = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            updateTargetTemperature(next.targetTemp);
            saveStateToEEPROM();
            
            Serial.printf("[Fase] ↪️  Indo para etapa %d/%d (%.1f°C)\n", 
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages,
                         next.targetTemp);
        } else {
            // Todas as etapas concluídas!
            Serial.println(F("[Fase] 🎉 TODAS AS ETAPAS CONCLUÍDAS!"));
            
            // Marca fermentação como concluída no MySQL
            JsonDocument doc;
            doc["status"] = "completed";
            doc["completedAt"] = "NOW()";
            doc["message"] = "Fermentação concluída automaticamente";
            
            String payload;
            serializeJson(doc, payload);
            httpClient.updateFermentationState(fermentacaoState.activeId, payload);
            
            // Desativa fermentação local
            deactivateCurrentFermentation();
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
// LEITURAS
// =====================================================
void enviarLeituraAtual() {
    if (!fermentacaoState.active || !isValidString(fermentacaoState.activeId)) {
        return;
    }

    float tempFridge = sensors.getTempCByIndex(1);
    float tempFermenter = state.currentTemp;
    float gravity = mySpindel.gravity;
    float tempTarget = fermentacaoState.tempTarget;
    
    if (httpClient.sendReading(fermentacaoState.activeId, tempFridge, 
                              tempFermenter, tempTarget, gravity)) {
        Serial.println(F("[MySQL] 📊 Leitura enviada"));
    } else {
        Serial.println(F("[MySQL] ❌ Falha ao enviar leitura"));
    }
}