// fermentacao_stages.cpp
#include <ArduinoJson.h>

#include "definitions.h"           // PRIMEIRO
#include "fermentacao_stages.h"    // Importa definitions.h (protegido)
#include "http_client.h"
#include "estruturas.h"
#include "globais.h"
#include "ispindel_struct.h"

// Variáveis globais
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern FermentadorHTTPClient httpClient;

// Variáveis globais
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern FermentadorHTTPClient httpClient;

// Intervalo para enviar estado ao MySQL (não afeta processamento local)
constexpr unsigned long STATE_UPDATE_INTERVAL = 5UL * 60UL * 1000UL; // 5 minutos
static unsigned long lastStateUpdate = 0;

// ========================================
// FUNÇÕES AUXILIARES
// ========================================

bool shouldUpdateState() {
    unsigned long now = millis();
    if (now - lastStateUpdate >= STATE_UPDATE_INTERVAL) {
        lastStateUpdate = now;
        return true;
    }
    return false;
}

void sendStateUpdate(const String& stateJson) {
    if (!httpClient.isConnected()) return;
    
    httpClient.updateFermentationState(fermentacaoState.activeId, stateJson);
    Serial.println(F("[Stages] 📤 Estado enviado ao MySQL"));
}

// ========================================
// ETAPA TIPO: TEMPERATURE (mantém temperatura por X dias)
// ========================================
// IMPORTANTE: Nome "temperature" é mantido por compatibilidade,
// mas funciona como controle de TEMPO após atingir temperatura alvo
bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays, bool targetReached) {
    
    // Se ainda não atingiu temperatura alvo, aguarda
    if (!targetReached) {
        if (shouldUpdateState()) {
            JsonDocument doc;
            doc["status"] = "waiting_target";
            doc["stageType"] = "temperature";
            doc["currentTargetTemp"] = stage.targetTemp;
            doc["timeRemaining"]["value"] = stage.durationDays;
            doc["timeRemaining"]["unit"] = "days";
            doc["timeRemaining"]["status"] = "waiting";
            doc["message"] = "Aguardando temperatura alvo";
            
            String payload;
            serializeJson(doc, payload);
            sendStateUpdate(payload);
        }
        return false; // Não avança etapa
    }
    
    // Temperatura alvo atingida, agora conta o tempo
    float remaining = (float)stage.durationDays - elapsedDays;
    if (remaining < 0) remaining = 0;

    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["status"] = "running";
        doc["stageType"] = "temperature";
        doc["currentTargetTemp"] = stage.targetTemp;
        doc["timeRemaining"]["value"] = remaining;
        doc["timeRemaining"]["unit"] = "days";
        doc["timeRemaining"]["status"] = remaining > 0 ? "running" : "completed";
        doc["progress"] = ((float)stage.durationDays - remaining) / (float)stage.durationDays * 100.0;
        
        String payload;
        serializeJson(doc, payload);
        sendStateUpdate(payload);
    }

    // Etapa concluída quando tempo total decorrido
    bool completed = elapsedDays >= (float)stage.durationDays;
    
    if (completed) {
        Serial.printf("[Stages] ✅ Etapa TEMPERATURE concluída: %.1f dias\n", elapsedDays);
    }
    
    return completed;
}

// ========================================
// ETAPA TIPO: RAMP (transição gradual de temperatura)
// ========================================
// Aumenta ou diminui temperatura gradualmente ao longo de X horas
bool handleRampStage(const FermentationStage& stage, float elapsedHours) {
    
    // Calcula progresso da rampa (0.0 a 1.0)
    float progress = 0.0;
    if (stage.rampTimeHours > 0) {
        progress = elapsedHours / (float)stage.rampTimeHours;
    }
    
    if (progress > 1.0) progress = 1.0;

    // Determina temperatura inicial (temperatura da etapa anterior)
    float tempInicial = stage.startTemp;
    if (fermentacaoState.currentStageIndex > 0 && stage.startTemp == 0.0) {
        // Se startTemp não foi setado, usa temperatura da etapa anterior
        tempInicial = fermentacaoState.stages[fermentacaoState.currentStageIndex - 1].targetTemp;
    }

    // Calcula temperatura atual baseada no progresso
    float tempAtual = tempInicial + (stage.targetTemp - tempInicial) * progress;
    
    // Atualiza temperatura alvo do sistema
    fermentacaoState.tempTarget = tempAtual;
    state.targetTemp = tempAtual;

    // Calcula tempo restante
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
        doc["timeRemaining"]["value"] = remaining;
        doc["timeRemaining"]["unit"] = "hours";
        doc["timeRemaining"]["status"] = remaining > 0 ? "running" : "completed";
        
        String payload;
        serializeJson(doc, payload);
        sendStateUpdate(payload);
    }

    // Etapa concluída quando tempo total decorrido
    bool completed = elapsedHours >= (float)stage.rampTimeHours;
    
    if (completed) {
        Serial.printf("[Stages] ✅ Rampa concluída: %.1f°C → %.1f°C em %.1f horas\n", 
                     tempInicial, stage.targetTemp, elapsedHours);
    }
    
    return completed;
}

// ========================================
// ETAPA TIPO: GRAVITY (aguarda gravidade específica)
// ========================================
// Mantém temperatura até iSpindel reportar gravidade <= alvo
bool handleGravityStage(const FermentationStage& stage) {
    
    // Verifica se temos dados válidos do iSpindel
    if (mySpindel.gravity <= 0 || mySpindel.gravity > 1.200) {
        if (shouldUpdateState()) {
            JsonDocument doc;
            doc["status"] = "waiting_gravity";
            doc["stageType"] = "gravity";
            doc["currentTargetTemp"] = stage.targetTemp;
            doc["targetGravity"] = stage.targetGravity;
            doc["currentGravity"] = 0.0;
            doc["message"] = "Aguardando leitura válida do iSpindel";
            
            String payload;
            serializeJson(doc, payload);
            sendStateUpdate(payload);
        }
        return false; // Não avança sem dados válidos
    }
    
    // Verifica se atingiu gravidade alvo
    bool gravityReached = (mySpindel.gravity <= stage.targetGravity);
    
    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["status"] = gravityReached ? "completed" : "running";
        doc["stageType"] = "gravity";
        doc["currentTargetTemp"] = stage.targetTemp;
        doc["targetGravity"] = stage.targetGravity;
        doc["currentGravity"] = mySpindel.gravity;
        doc["gravityDiff"] = mySpindel.gravity - stage.targetGravity;
        
        if (gravityReached) {
            doc["message"] = "Gravidade alvo atingida";
        } else {
            doc["message"] = "Fermentando...";
        }
        
        String payload;
        serializeJson(doc, payload);
        sendStateUpdate(payload);
    }
    
    if (gravityReached) {
        Serial.printf("[Stages] 🎯 Gravidade alvo atingida: %.3f <= %.3f\n", 
                     mySpindel.gravity, stage.targetGravity);
    }
    
    return gravityReached;
}

// ========================================
// ETAPA TIPO: GRAVITY_TIME (gravidade com timeout)
// ========================================
// Aguarda gravidade OU timeout (o que ocorrer primeiro)
bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays) {
    
    // Verifica se tem dados válidos do iSpindel
    bool hasValidGravity = (mySpindel.gravity > 0 && mySpindel.gravity <= 1.200);
    
    // Verifica se atingiu gravidade (se tiver dados válidos)
    bool gravityReached = false;
    if (hasValidGravity) {
        gravityReached = (mySpindel.gravity <= stage.targetGravity);
    }
    
    // Calcula tempo restante
    float remainingDays = (float)stage.timeoutDays - elapsedDays;
    if (remainingDays < 0) remainingDays = 0;
    
    // Verifica se atingiu timeout
    bool timeoutReached = (elapsedDays >= (float)stage.timeoutDays);
    
    if (shouldUpdateState()) {
        JsonDocument doc;
        doc["stageType"] = "gravity_time";
        doc["currentTargetTemp"] = stage.targetTemp;
        doc["targetGravity"] = stage.targetGravity;
        doc["timeoutDays"] = stage.timeoutDays;
        doc["timeRemaining"]["value"] = remainingDays;
        doc["timeRemaining"]["unit"] = "days";
        
        if (hasValidGravity) {
            doc["currentGravity"] = mySpindel.gravity;
            doc["gravityDiff"] = mySpindel.gravity - stage.targetGravity;
        } else {
            doc["currentGravity"] = 0.0;
            doc["message"] = "Aguardando leitura do iSpindel";
        }
        
        if (gravityReached) {
            doc["status"] = "completed";
            doc["completionReason"] = "gravity_reached";
            doc["message"] = "Gravidade alvo atingida";
        } else if (timeoutReached) {
            doc["status"] = "completed";
            doc["completionReason"] = "timeout";
            doc["message"] = "Timeout atingido";
        } else {
            doc["status"] = "running";
            doc["message"] = "Fermentando...";
        }
        
        String payload;
        serializeJson(doc, payload);
        sendStateUpdate(payload);
    }
    
    // Etapa concluída se atingiu gravidade OU timeout
    bool completed = gravityReached || timeoutReached;
    
    if (completed) {
        if (gravityReached) {
            Serial.printf("[Stages] 🎯 Gravidade atingida: %.3f <= %.3f (%.1f dias)\n", 
                         mySpindel.gravity, stage.targetGravity, elapsedDays);
        } else {
            Serial.printf("[Stages] ⏰ Timeout atingido: %.1f dias (gravidade: %.3f)\n", 
                         elapsedDays, hasValidGravity ? mySpindel.gravity : 0.0);
        }
    }
    
    return completed;
}

// ========================================
// VERIFICAÇÃO SE TEMPERATURA ALVO FOI ATINGIDA
// ========================================
// Usado para etapas que precisam atingir temperatura antes de contar tempo
void checkAndSendTargetReached() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) {
        return;
    }

    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) {
        fermentacaoState.targetReachedSent = true;
        
        // Notifica o MySQL
        if (httpClient.notifyTargetReached(fermentacaoState.activeId)) {
            Serial.println(F("[Stages] 🎯 Temperatura alvo atingida! MySQL notificado."));
        }
    }
}

// ========================================
// PROCESSADOR PRINCIPAL DE ETAPAS
// ========================================
// Chamado periodicamente pelo loop principal
bool processCurrentStage(const FermentationStage& stage, float elapsedDays, 
                        float elapsedHours, bool targetReached) {
    
    switch (stage.type) {
        case STAGE_TEMPERATURE:
            // Etapa de tempo: aguarda temp alvo + mantém por X dias
            return handleTemperatureStage(stage, elapsedDays, targetReached);

        case STAGE_RAMP:
            // Transição gradual de temperatura
            return handleRampStage(stage, elapsedHours);

        case STAGE_GRAVITY:
            // Aguarda gravidade específica (sem timeout)
            return handleGravityStage(stage);

        case STAGE_GRAVITY_TIME:
            // Aguarda gravidade OU timeout
            return handleGravityTimeStage(stage, elapsedDays);

        default:
            Serial.printf("[Stages] ❌ Tipo de etapa desconhecido: %d\n", stage.type);
            return false; // Não avança em caso de erro
    }
}

// ========================================
// ENVIO DE RESUMO DAS ETAPAS (inicial)
// ========================================
// Envia resumo de todas as etapas ao iniciar fermentação
void sendStagesSummary() {
    if (!httpClient.isConnected()) return;
    
    JsonDocument doc;
    doc["totalStages"] = fermentacaoState.totalStages;
    doc["currentStageIndex"] = fermentacaoState.currentStageIndex;
    
    JsonArray stagesArray = doc["stages"].to<JsonArray>();
    
    for (int i = 0; i < fermentacaoState.totalStages; i++) {
        const FermentationStage& stage = fermentacaoState.stages[i];
        
        JsonObject stageObj = stagesArray.add<JsonObject>();
        stageObj["index"] = i;
        
        switch (stage.type) {
            case STAGE_TEMPERATURE:
                stageObj["type"] = "temperature";
                stageObj["duration"] = String(stage.durationDays) + "d";
                break;
            case STAGE_RAMP:
                stageObj["type"] = "ramp";
                stageObj["duration"] = String(stage.rampTimeHours) + "h";
                break;
            case STAGE_GRAVITY:
                stageObj["type"] = "gravity";
                stageObj["targetGravity"] = stage.targetGravity;
                break;
            case STAGE_GRAVITY_TIME:
                stageObj["type"] = "gravity_time";
                stageObj["targetGravity"] = stage.targetGravity;
                stageObj["timeout"] = String(stage.timeoutDays) + "d";
                break;
        }
        
        stageObj["targetTemp"] = stage.targetTemp;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    httpClient.updateFermentationState(fermentacaoState.activeId, payload);
    Serial.println(F("[Stages] 📋 Resumo das etapas enviado"));
}