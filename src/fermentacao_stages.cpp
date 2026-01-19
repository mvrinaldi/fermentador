// fermentacao_stages.cpp - Otimizado para ESP8266 e ArduinoJson v7
#include <Arduino.h>
#include "definitions.h"
#include "fermentacao_stages.h"
#include "http_client.h"
#include "estruturas.h"
#include "globais.h"
#include "ispindel_struct.h"
#include "debug_config.h"  // Adicionado para ter DEBUG_FERMENTATION

// Variáveis globais externas
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern FermentadorHTTPClient httpClient;

// Intervalo para enviar estado ao servidor (5 minutos) [3]
constexpr unsigned long STATE_UPDATE_INTERVAL = 5UL * 60UL * 1000UL;
static unsigned long lastStateUpdate = 0;

// ========================================
// FUNÇÕES AUXILIARES
// ========================================

// Verifica se é hora de enviar uma atualização de status ao MySQL [4]
bool shouldUpdateState() {
    unsigned long now = millis();
    if (now - lastStateUpdate >= STATE_UPDATE_INTERVAL) {
        lastStateUpdate = now;
        return true;
    }
    return false;
}

// CORREÇÃO: Agora aceita JsonDocument por referência para evitar cópias de String na RAM [1]
void sendStateUpdate(const JsonDocument& doc) {
    if (!httpClient.isConnected()) return;

    // Chama o cliente passando o documento diretamente
    httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[Stages] 📤 Estado enviado ao servidor (JSON v7)"));
    #endif
}

// ========================================
// ETAPA TIPO: TEMPERATURE
// ========================================
bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays, bool targetReached) {
    if (!targetReached) {
        if (shouldUpdateState()) {
            JsonDocument doc; // Alocação automática v7
            doc["status"] = "waiting_target";
            doc["stageType"] = "temperature";
            doc["currentTargetTemp"] = stage.targetTemp;
            
            JsonObject timeRem = doc["timeRemaining"].to<JsonObject>();
            timeRem["value"] = stage.durationDays;
            timeRem["unit"] = "days";
            timeRem["status"] = "waiting";
            
            doc["message"] = "Aguardando temperatura alvo";
            sendStateUpdate(doc);
        }
        return false; 
    }

    float remaining = (float)stage.durationDays - elapsedDays;
    if (remaining < 0) remaining = 0;

    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["status"] = "running";
        doc["stageType"] = "temperature";
        doc["currentTargetTemp"] = stage.targetTemp;
        
        JsonObject timeRem = doc["timeRemaining"].to<JsonObject>();
        timeRem["value"] = remaining;
        timeRem["unit"] = "days";
        timeRem["status"] = remaining > 0 ? "running" : "completed";
        
        doc["progress"] = ((float)stage.durationDays - remaining) / (float)stage.durationDays * 100.0;
        sendStateUpdate(doc);
    }

    bool completed = elapsedDays >= (float)stage.durationDays;
    
    #if DEBUG_FERMENTATION
    if (completed) {
        Serial.printf("[Stages] ✅ Etapa TEMPERATURE concluída: %.1f dias\n", elapsedDays);
    }
    #endif
    
    return completed;
}

// ========================================
// ETAPA TIPO: RAMP
// ========================================
bool handleRampStage(const FermentationStage& stage, float elapsedHours) {
    float progress = 0.0;
    if (stage.rampTimeHours > 0) {
        progress = elapsedHours / (float)stage.rampTimeHours;
    }
    if (progress > 1.0) progress = 1.0;

    float tempInicial = stage.startTemp;
    if (fermentacaoState.currentStageIndex > 0 && stage.startTemp == 0.0) {
        tempInicial = fermentacaoState.stages[fermentacaoState.currentStageIndex - 1].targetTemp;
    }

    float tempAtual = tempInicial + (stage.targetTemp - tempInicial) * progress;

    // Atualiza temperatura alvo do sistema para o BrewPi [5]
    fermentacaoState.tempTarget = tempAtual;
    state.targetTemp = tempAtual;

    float remaining = (float)stage.rampTimeHours - elapsedHours;
    if (remaining < 0) remaining = 0;

    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["status"] = "running";
        doc["stageType"] = "ramp";
        doc["currentTargetTemp"] = tempAtual;
        doc["startTemp"] = tempInicial;
        doc["endTemp"] = stage.targetTemp;
        doc["rampProgress"] = progress * 100.0;

        JsonObject timeRem = doc["timeRemaining"].to<JsonObject>();
        timeRem["value"] = remaining;
        timeRem["unit"] = "hours";
        timeRem["status"] = remaining > 0 ? "running" : "completed";
        
        sendStateUpdate(doc);
    }

    bool completed = elapsedHours >= (float)stage.rampTimeHours;
    
    #if DEBUG_FERMENTATION
    if (completed) {
        Serial.printf("[Stages] ✅ Rampa concluída em %.1f horas\n", elapsedHours);
    }
    #endif
    
    return completed;
}

// ========================================
// ETAPA TIPO: GRAVITY
// ========================================
bool handleGravityStage(const FermentationStage& stage) {
    if (mySpindel.gravity <= 0 || mySpindel.gravity > 1.200) {
        if (shouldUpdateState()) {
            JsonDocument doc;
            doc["status"] = "waiting_gravity";
            doc["stageType"] = "gravity";
            doc["currentTargetTemp"] = stage.targetTemp;
            doc["targetGravity"] = stage.targetGravity;
            doc["message"] = "Aguardando iSpindel";
            sendStateUpdate(doc);
        }
        return false;
    }

    bool gravityReached = (mySpindel.gravity <= stage.targetGravity);

    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["status"] = gravityReached ? "completed" : "running";
        doc["stageType"] = "gravity";
        doc["currentTargetTemp"] = stage.targetTemp;
        doc["targetGravity"] = stage.targetGravity;
        doc["currentGravity"] = mySpindel.gravity;
        sendStateUpdate(doc);
    }

    return gravityReached;
}

// ========================================
// ETAPA TIPO: GRAVITY_TIME
// ========================================
bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays) {
    bool hasValidGravity = (mySpindel.gravity > 0 && mySpindel.gravity <= 1.200);
    bool gravityReached = hasValidGravity && (mySpindel.gravity <= stage.targetGravity);
    bool timeoutReached = (elapsedDays >= (float)stage.timeoutDays);
    float remainingDays = (float)stage.timeoutDays - elapsedDays;

    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["stageType"] = "gravity_time";
        doc["currentTargetTemp"] = stage.targetTemp;
        doc["targetGravity"] = stage.targetGravity;
        
        JsonObject timeRem = doc["timeRemaining"].to<JsonObject>();
        timeRem["value"] = remainingDays < 0 ? 0 : remainingDays;
        timeRem["unit"] = "days";

        if (gravityReached) {
            doc["status"] = "completed";
            doc["completionReason"] = "gravity_reached";
        } else if (timeoutReached) {
            doc["status"] = "completed";
            doc["completionReason"] = "timeout";
        } else {
            doc["status"] = "running";
        }
        sendStateUpdate(doc);
    }

    return gravityReached || timeoutReached;
}

// ========================================
// PROCESSADOR PRINCIPAL E RESUMO
// ========================================

void checkAndSendTargetReached() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);
    if (diff <= TEMPERATURE_TOLERANCE) {
        fermentacaoState.targetReachedSent = true;
        if (httpClient.notifyTargetReached(fermentacaoState.activeId)) { // [6]
            #if DEBUG_FERMENTATION
            Serial.println(F("[Stages] 🎯 Alvo atingido notificado"));
            #endif
        }
    }
}

bool processCurrentStage(const FermentationStage& stage, float elapsedDays, float elapsedHours, bool targetReached) {
    switch (stage.type) {
        case STAGE_TEMPERATURE: return handleTemperatureStage(stage, elapsedDays, targetReached);
        case STAGE_RAMP:        return handleRampStage(stage, elapsedHours);
        case STAGE_GRAVITY:     return handleGravityStage(stage);
        case STAGE_GRAVITY_TIME:return handleGravityTimeStage(stage, elapsedDays);
        default:                return false;
    }
}

void sendStagesSummary() {
    if (!httpClient.isConnected()) return;

    JsonDocument doc; // ArduinoJson v7 reserva memória dinamicamente
    doc["totalStages"] = fermentacaoState.totalStages;
    doc["currentStageIndex"] = fermentacaoState.currentStageIndex;

    JsonArray stagesArray = doc["stages"].to<JsonArray>();

    for (int i = 0; i < fermentacaoState.totalStages; i++) {
        const FermentationStage& stage = fermentacaoState.stages[i];
        JsonObject stageObj = stagesArray.add<JsonObject>();
        stageObj["index"] = i;
        stageObj["targetTemp"] = stage.targetTemp;

        switch (stage.type) {
            case STAGE_TEMPERATURE: 
                stageObj["type"] = "temperature"; 
                stageObj["duration"] = stage.durationDays;
                break;
            case STAGE_RAMP:        
                stageObj["type"] = "ramp"; 
                stageObj["durationHours"] = stage.rampTimeHours;
                break;
            case STAGE_GRAVITY:     
                stageObj["type"] = "gravity"; 
                stageObj["targetGravity"] = stage.targetGravity;
                break;
            case STAGE_GRAVITY_TIME:
                stageObj["type"] = "gravity_time";
                stageObj["targetGravity"] = stage.targetGravity;
                stageObj["timeout"] = stage.timeoutDays;
                break;
        }
    }

    // CORREÇÃO: Envia o documento diretamente sem converter para String manualmente [1, 7]
    httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[Stages] 📋 Resumo enviado"));
    #endif
}