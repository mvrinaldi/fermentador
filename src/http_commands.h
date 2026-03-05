// http_commands.h - Gerenciamento de comandos HTTP do servidor
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "http_client.h"
#include "preferences_utils.h"

inline void checkPendingCommands() {
    if (!fermentacaoState.active || !isValidString(fermentacaoState.activeId)) return;

    String command = httpClient.getPendingCommand(atoi(fermentacaoState.activeId));
    if (command.length() == 0) return;

    Serial.printf("[CMD] Executando: %s\n", command.c_str());

    if (command == "CLEAR_EEPROM") {
        clearAllPreferencesUtil();

    } else if (command == "ADVANCE_STAGE") {
        int nextIndex = fermentacaoState.currentStageIndex + 1;

        if (nextIndex >= fermentacaoState.totalStages) {
            Serial.println(F("[CMD] ADVANCE_STAGE ignorado: já está na última etapa"));
            return;
        }

        fermentacaoState.currentStageIndex     = nextIndex;
        stageStarted                           = false;
        fermentacaoState.stageStartEpoch       = 0;
        fermentacaoState.targetReachedSent     = false;
        fermentacaoState.concluidaMantendoTemp = false;

        float newTarget = fermentacaoState.stages[nextIndex].targetTemp;
        if (fermentacaoState.stages[nextIndex].type == STAGE_RAMP) {
            newTarget = fermentacaoState.stages[nextIndex].startTemp;
        }
        updateTargetTemperature(newTarget);
        brewPiControl.reset();
        saveStateToPreferences();

        httpClient.updateStageIndex(fermentacaoState.activeId, nextIndex);

        Serial.printf("[CMD] ✅ Avançado para etapa %d/%d (alvo: %.1f°C)\n",
                      nextIndex + 1, fermentacaoState.totalStages, newTarget);
    }
}