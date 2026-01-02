#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "globais.h"

// Throttle de atualização no Firebase
bool canSendFirebaseUpdate();

// Envio padronizado para o Firebase
void updateFermentationState(JsonDocument& doc);

// Processamento da etapa atual
bool processCurrentStage(
    const FermentationStage& stage,
    float elapsedDays,
    float elapsedHours
);

// Handlers por tipo de etapa
bool handleTemperatureStage(
    const FermentationStage& stage,
    float elapsedDays
);

bool handleRampStage(
    const FermentationStage& stage,
    float elapsedHours
);

bool handleGravityStage(
    const FermentationStage& stage
);

bool handleGravityTimeStage(
    const FermentationStage& stage,
    float elapsedDays
);

void sendStageTimers();