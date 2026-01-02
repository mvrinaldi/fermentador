#include "fermentacao_stages.h"
#include "firebase_conexao.h"
#include <ESP8266WiFi.h>

constexpr unsigned long FIREBASE_UPDATE_INTERVAL =
    15UL * 60UL * 1000UL; // 15 minutos

static unsigned long lastFirebaseUpdate = 0;

// Variável estática para controlar se já enviamos o timestamp nesta etapa
static bool targetReachedSent = false;

bool canSendFirebaseUpdate() {
    unsigned long now = millis();
    if (now - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL) {
        lastFirebaseUpdate = now;
        return true;
    }
    return false;
}

void updateFermentationState(JsonDocument& doc) {
    if (!canSendFirebaseUpdate()) return; // Mantém o intervalo de 15 min [3]
    if (WiFi.status() != WL_CONNECTED) return;

    String payload;
    serializeJson(doc, payload);

    // Usa update para não apagar outros campos existentes no nó [4]
    Database.update(aClient, "/fermentationState", payload);
    Serial.println(F("[Firebase] 🔄 Estado atualizado"));
}

bool handleTemperatureStage(
    const FermentationStage& stage,
    float elapsedDays
) {
    float remaining = (float)stage.durationDays - elapsedDays;

    JsonDocument doc;
    doc["timeRemaining"]["value"] = remaining > 0 ? remaining : 0;
    doc["timeRemaining"]["unit"] = "days";
    doc["timeRemaining"]["status"] =
        (remaining <= 0) ? "completed" : "running";
    doc["currentTargetTemp"] = stage.targetTemp;

    updateFermentationState(doc);

    return elapsedDays >= (float)stage.durationDays;
}

void checkAndSendTargetReached() {
    // Verifica se a temperatura atual está dentro da tolerância do alvo [4, 5]
    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);
    
    if (!targetReachedSent && diff <= TEMPERATURE_TOLERANCE) {
        JsonDocument doc;
        doc["targetReachedTime"][".sv"] = "timestamp"; // Marca de tempo do servidor
        updateFermentationState(doc);
        targetReachedSent = true;
    }
}

void sendStageTimers() {
    if (WiFi.status() != WL_CONNECTED) return;

    JsonDocument doc;
    // Cria o objeto com os tempos planejados de cada etapa [1, 5]
    for (int i = 0; i < fermentacaoState.totalStages; i++) {
        String key = "stage_" + String(i);
        if (fermentacaoState.stages[i].type == STAGE_RAMP) {
            doc["stageTimers"][key] = String(fermentacaoState.stages[i].rampTimeHours) + "h";
        } else {
            doc["stageTimers"][key] = String(fermentacaoState.stages[i].durationDays) + "d";
        }
    }

    String payload;
    serializeJson(doc, payload);
    // Envia imediatamente sem passar pelo filtro de 15 minutos
    Database.update(aClient, "/fermentationState", payload);
}

bool handleRampStage(
    const FermentationStage& stage,
    float elapsedHours
) {
    float divisor = stage.rampTimeHours > 0
                        ? (float)stage.rampTimeHours
                        : 1.0;

    float progress = elapsedHours / divisor;
    if (progress > 1.0) progress = 1.0;

    float tempInicial = (fermentacaoState.currentStageIndex > 0)
        ? fermentacaoState.stages[
            fermentacaoState.currentStageIndex - 1
          ].targetTemp
        : stage.startTemp;

    float tempAtual = tempInicial +
        (stage.targetTemp - tempInicial) * progress;

    fermentacaoState.tempTarget = tempAtual;
    state.targetTemp = tempAtual;

    float remaining = stage.rampTimeHours - elapsedHours;

    JsonDocument doc;
    doc["timeRemaining"]["value"] = remaining > 0 ? remaining : 0;
    doc["timeRemaining"]["unit"] = "hours";
    doc["timeRemaining"]["status"] =
        (remaining <= 0) ? "completed" : "running";
    doc["currentTargetTemp"] = tempAtual;

    updateFermentationState(doc);

    return elapsedHours >= (float)stage.rampTimeHours;
}

bool handleGravityStage(
    const FermentationStage& stage
) {
    return mySpindel.gravity > 0 &&
           mySpindel.gravity <= stage.targetGravity;
}

bool handleGravityTimeStage(
    const FermentationStage& stage,
    float elapsedDays
) {
    if (mySpindel.gravity > 0 &&
        mySpindel.gravity <= stage.targetGravity) {
        return true;
    }

    return elapsedDays >= (float)stage.timeoutDays;
}

bool processCurrentStage(
    const FermentationStage& stage,
    float elapsedDays,
    float elapsedHours
) {
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
            return false;
    }
}