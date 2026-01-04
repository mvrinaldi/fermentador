#include "fermentacao_stages.h"
#include "firebase_conexao.h"
#include "estruturas.h"
#include "globais.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "ispindel_struct.h"

// Declaração das estruturas (já que usamos forward declaration no header)
struct FermentationStage;
struct FermentacaoState;
struct SystemState;

// Variáveis globais (definidas em outros arquivos)
extern FermentacaoState fermentacaoState;
extern SystemState state;

constexpr unsigned long FIREBASE_UPDATE_INTERVAL = 15UL * 60UL * 1000UL; // 15 minutos

static unsigned long lastFirebaseUpdate = 0;

// Funções auxiliares (não exportadas no header)
bool canSendFirebaseUpdate();
static void updateFermentationState(ArduinoJson::JsonDocument& doc);
static bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays);
static bool handleRampStage(const FermentationStage& stage, float elapsedHours);
static bool handleGravityStage(const FermentationStage& stage);
static bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays);

// Implementações...

bool canSendFirebaseUpdate() {
    unsigned long now = millis();
    if (now - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL) {
        lastFirebaseUpdate = now;
        return true;
    }
    return false;
}

void updateFermentationState(ArduinoJson::JsonDocument& doc) {
    if (!canSendFirebaseUpdate()) return;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[Firebase] ⚠️  WiFi desconectado"));
        return;
    }

    String payload;
    serializeJson(doc, payload);

    if (Database.update(aClient, "/fermentationState", payload)) {
        Serial.println(F("[Firebase] 🔄 Estado atualizado"));
    } else {
        Serial.println(F("[Firebase] ❌ Falha ao atualizar estado"));
    }
}

bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays) {
    float remaining = (float)stage.durationDays - elapsedDays;
    if (remaining < 0) remaining = 0;

    ArduinoJson::JsonDocument doc;
    doc["timeRemaining"]["value"] = remaining;
    doc["timeRemaining"]["unit"] = "days";
    doc["timeRemaining"]["status"] = (remaining <= 0) ? "completed" : "running";
    doc["currentTargetTemp"] = stage.targetTemp;

    updateFermentationState(doc);

    return elapsedDays >= (float)stage.durationDays;
}

void checkAndSendTargetReached() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) {
        return;
    }

    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) {
        ArduinoJson::JsonDocument doc;
        doc["targetReachedTime"][".sv"] = "timestamp";
        updateFermentationState(doc);
        fermentacaoState.targetReachedSent = true;
        Serial.println(F("[Stages] 🎯 Temperatura alvo atingida! Notificação enviada."));
    }
}

void sendStageTimers() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[Firebase] ⚠️  WiFi desconectado - não é possível enviar timers"));
        return;
    }

    ArduinoJson::JsonDocument doc;
    
    for (int i = 0; i < fermentacaoState.totalStages; i++) {
        String key = "stage_" + String(i);
        const FermentationStage& stage = fermentacaoState.stages[i];
        
        if (stage.type == STAGE_RAMP) {
            doc["stageTimers"][key] = String(stage.rampTimeHours) + "h";
        } else {
            doc["stageTimers"][key] = String(stage.durationDays) + "d";
        }
    }

    String payload;
    serializeJson(doc, payload);
    
    if (Database.update(aClient, "/fermentationState", payload)) {
        Serial.println(F("[Firebase] ⏱️  Timers das etapas enviados"));
    } else {
        Serial.println(F("[Firebase] ❌ Falha ao enviar timers"));
    }
}

bool handleRampStage(const FermentationStage& stage, float elapsedHours) {
    float divisor = (stage.rampTimeHours > 0) ? (float)stage.rampTimeHours : 1.0;
    float progress = elapsedHours / divisor;
    
    if (progress > 1.0) progress = 1.0;

    float tempInicial = stage.startTemp;
    if (fermentacaoState.currentStageIndex > 0) {
        tempInicial = fermentacaoState.stages[fermentacaoState.currentStageIndex - 1].targetTemp;
    }

    float tempAtual = tempInicial + (stage.targetTemp - tempInicial) * progress;
    
    fermentacaoState.tempTarget = tempAtual;
    state.targetTemp = tempAtual;

    float remaining = stage.rampTimeHours - elapsedHours;
    if (remaining < 0) remaining = 0;

    ArduinoJson::JsonDocument doc;
    doc["timeRemaining"]["value"] = remaining;
    doc["timeRemaining"]["unit"] = "hours";
    doc["timeRemaining"]["status"] = (remaining <= 0) ? "completed" : "running";
    doc["currentTargetTemp"] = tempAtual;
    doc["rampProgress"] = progress * 100;

    updateFermentationState(doc);

    return elapsedHours >= (float)stage.rampTimeHours;
}

bool handleGravityStage(const FermentationStage& stage) {
    if (mySpindel.gravity <= 0) {
        return false;
    }
    
    bool gravityReached = (mySpindel.gravity <= stage.targetGravity);
    
    if (gravityReached) {
        Serial.printf("[Stages] 🎯 Gravidade alvo atingida: %.3f <= %.3f\n", 
                     mySpindel.gravity, stage.targetGravity);
    }
    
    return gravityReached;
}

bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays) {
    if (mySpindel.gravity > 0 && mySpindel.gravity <= stage.targetGravity) {
        Serial.printf("[Stages] 🎯 Gravidade alvo atingida antes do timeout: %.3f\n", 
                     mySpindel.gravity);
        return true;
    }

    bool timeoutReached = (elapsedDays >= (float)stage.timeoutDays);
    
    if (timeoutReached) {
        Serial.printf("[Stages] ⏰ Timeout atingido: %.1f dias\n", elapsedDays);
    }
    
    return timeoutReached;
}

bool processCurrentStage(const FermentationStage& stage, float elapsedDays, float elapsedHours) {
    switch (stage.type) {
        case STAGE_TEMPERATURE:
            return handleTemperatureStage(stage, elapsedDays);

        case STAGE_RAMP:
            return handleRampStage(stage, elapsedHours);

        case STAGE_GRAVITY:
            return handleGravityStage(stage);

        case STAGE_GRAVITY_TIME:
            return handleGravityTimeStage(stage, elapsedDays);

        default:
            Serial.printf("[Stages] ⚠️  Tipo de etapa desconhecido: %d\n", stage.type);
            return false;
    }
}