// fermentacao_stages.cpp - Processamento local de etapas
// ✅ CONSOLIDADO: Apenas lógica de processamento, sem envios diretos
// Envios são feitos por enviarEstadoCompletoMySQL() em mysql_sender.cpp
#include <Arduino.h>
#include "definitions.h"
#include "fermentacao_stages.h"
#include "mysql_sender.h"
#include "http_client.h"
#include "estruturas.h"
#include "globais.h"
#include "ispindel_struct.h"
#include "debug_config.h"

// Variáveis globais externas
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern FermentadorHTTPClient httpClient;

// ========================================
// ETAPA TIPO: TEMPERATURE
// Apenas processa lógica, sem envio
// ========================================
bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays, bool targetReached) {
    if (!targetReached) {
        return false; 
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
// Apenas processa lógica, sem envio
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

    // Atualiza temperatura alvo do sistema para o BrewPi
    fermentacaoState.tempTarget = tempAtual;
    state.targetTemp = tempAtual;

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
// Apenas processa lógica, sem envio
// ========================================
bool handleGravityStage(const FermentationStage& stage) {
    bool hasValidGravity = (mySpindel.gravity > 0 && mySpindel.gravity <= 1.200);
    
    if (!hasValidGravity) {
        return false;
    }
    
    bool gravityReached = (mySpindel.gravity <= stage.targetGravity);

    #if DEBUG_FERMENTATION
    if (gravityReached) {
        Serial.printf("[Stages] ✅ Gravidade alvo atingida: %.3f\n", mySpindel.gravity);
    }
    #endif

    return gravityReached;
}

// ========================================
// ETAPA TIPO: GRAVITY_TIME
// Apenas processa lógica, sem envio
// ========================================
bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays) {
    bool hasValidGravity = (mySpindel.gravity > 0 && mySpindel.gravity <= 1.200);
    bool gravityReached = hasValidGravity && (mySpindel.gravity <= stage.targetGravity);
    bool timeoutReached = (elapsedDays >= (float)stage.timeoutDays);

    #if DEBUG_FERMENTATION
    if (gravityReached) {
        Serial.printf("[Stages] ✅ Gravidade alvo atingida: %.3f\n", mySpindel.gravity);
    } else if (timeoutReached) {
        Serial.printf("[Stages] ⏰ Timeout atingido: %.1f dias\n", elapsedDays);
    }
    #endif

    return gravityReached || timeoutReached;
}

// ========================================
// PROCESSADOR PRINCIPAL
// ========================================

void checkAndSendTargetReached() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);
    if (diff <= TEMPERATURE_TOLERANCE) {
        fermentacaoState.targetReachedSent = true;
        if (httpClient.notifyTargetReached(fermentacaoState.activeId)) {
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

// Wrapper para manter compatibilidade
void sendStagesSummary() {
    sendStagesSummaryMySQL();
}
