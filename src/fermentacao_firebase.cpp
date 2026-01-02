#define ENABLE_DATABASE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <cstring>

#include "firebase_conexao.h"
#include "fermentacao_firebase.h"
#include "globais.h"
#include "eeprom_layout.h"
#include "fermentacao_stages.h"
#include "estruturas.h"

// =====================================================
// VARIÁVEIS DE CONTROLE
// =====================================================
unsigned long lastActiveCheck = 0;
char lastActiveId[64] = "";
bool isFirstCheck = true;
bool listenerSetup = false;
unsigned long lastListenerCheck = 0;
bool targetReachedSent = false; 

// Controle de fases
unsigned long stageStartTime = 0;
bool stageStarted = false;

// =====================================================
// EEPROM
// =====================================================
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    // ID ativo (64–95)
    size_t len = strlen(fermentacaoState.activeId);
    for (unsigned int i = 0; i < 32; i++) {
        EEPROM.write(
            ADDR_ACTIVE_ID + i,
            i < len ? fermentacaoState.activeId[i] : 0
        );
    }

    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    unsigned long long startMillis = stageStartTime;
    EEPROM.put(ADDR_STAGE_START_TIME, startMillis);

    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);

    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    EEPROM.commit();

    Serial.println(F("[EEPROM] ✅ Estado salvo"));
}

void loadStateFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    if (EEPROM.read(ADDR_CONFIG_SAVED) != 1) {
        Serial.println(F("[EEPROM] Nenhum estado salvo"));
        return;
    }

    for (int i = 0; i < 32; i++) {
        fermentacaoState.activeId[i] =
            EEPROM.read(ADDR_ACTIVE_ID + i);
    }
    fermentacaoState.activeId[31] = '\0';

    EEPROM.get(
        ADDR_STAGE_INDEX,
        fermentacaoState.currentStageIndex
    );

    unsigned long long startMillis;
    EEPROM.get(ADDR_STAGE_START_TIME, startMillis);
    stageStartTime = (unsigned long)startMillis;

    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);

    fermentacaoState.active = strlen(fermentacaoState.activeId) > 0;

    Serial.println(F("[EEPROM] ✅ Estado restaurado"));
}

void clearEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = ADDR_FERMENTATION_START; i <= 127; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    Serial.println(F("[EEPROM] 🧹 Seção de fermentação limpa"));
}

// =====================================================
// CONTROLE DE ESTADO
// =====================================================
void updateTargetTemperature(float temp) {
    fermentacaoState.tempTarget = temp;
    state.targetTemp = temp;
}

void deactivateCurrentFermentation() {
    Serial.println(F("[Firebase] 🧹 Desativando fermentação"));

    fermentacaoState.clear();
    lastActiveId[0] = '\0';

    stageStartTime = 0;
    stageStarted = false;

    updateTargetTemperature(20.0);
    clearEEPROM();
}

void setupActiveListener() {
    if (listenerSetup) return;

    listenerSetup = true;
    Serial.println(F("[Firebase] Listener ativo"));
    loadStateFromEEPROM();
}

void keepListenerAlive() {
    unsigned long now = millis();
    if (now - lastListenerCheck >= 60000) {
        lastListenerCheck = now;
        getTargetFermentacao();
    }
}

// =====================================================
// FIREBASE – FERMENTAÇÃO ATIVA
// =====================================================
void getTargetFermentacao() {
    unsigned long now = millis();

    if (!isFirstCheck &&
        (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    Serial.println(F("[Firebase] Buscando fermentação ativa"));

    String result = Database.get<String>(aClient, "/active");
    if (result.isEmpty()) {
        Serial.println(F("[Firebase] Sem resposta"));
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, result)) {
        Serial.println(F("[Firebase] JSON inválido"));
        return;
    }

    bool active = doc["active"] | false;
    const char* id = doc["id"] | "";

    if (active && strlen(id) > 0) {

        if (strcmp(id, lastActiveId) != 0) {
            Serial.println(F("[Firebase] 🎯 Nova fermentação"));

            fermentacaoState.active = true;
            strncpy(
                fermentacaoState.activeId,
                id,
                sizeof(fermentacaoState.activeId) - 1
            );
            fermentacaoState.activeId[63] = '\0';

            fermentacaoState.currentStageIndex = 0;
            strncpy(
                lastActiveId,
                id,
                sizeof(lastActiveId) - 1
            );
            lastActiveId[sizeof(lastActiveId) - 1] = '\0';

            loadConfigParameters(id);

            stageStartTime = 0;
            stageStarted = false;

            saveStateToEEPROM();
        }

    } else if (fermentacaoState.active && !active) {
        deactivateCurrentFermentation();
    }

    isFirstCheck = false;
}

// =====================================================
// CONFIGURAÇÃO DE ETAPAS
// =====================================================
void loadConfigParameters(const char* configId) {
    if (!configId || strlen(configId) == 0) return;

    String path = String("/configurations/") + configId;
    Serial.printf("[Firebase] Config: %s\n", path.c_str());

    String result = Database.get<String>(aClient, path.c_str());
    if (result.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, result)) return;

    fermentacaoState.currentStageIndex =
        doc["currentStageIndex"] | 0;

    const char* name = doc["name"] | "Sem nome";
    strncpy(
        fermentacaoState.configName,
        name,
        sizeof(fermentacaoState.configName) - 1
    );
    fermentacaoState.configName[
        sizeof(fermentacaoState.configName) - 1
    ] = '\0';

    JsonArray stages = doc["stages"];
    int count = 0;

    for (JsonVariant stage : stages) {
        if (count >= MAX_STAGES) break;

        FermentationStage& s =
            fermentacaoState.stages[count];

        const char* type = stage["type"] | "temperature";

        s.targetTemp     = stage["targetTemp"] | 20.0;
        s.startTemp      = stage["startTemp"] | 20.0;
        s.rampTimeHours  = stage["rampTime"] | 0;
        s.durationDays   = stage["durationDays"] | 0;
        s.targetGravity  = stage["targetGravity"] | 0.0;
        s.timeoutDays    = stage["timeoutDays"] | 0;

        if (!strcmp(type, "ramp")) s.type = STAGE_RAMP;
        else if (!strcmp(type, "gravity")) s.type = STAGE_GRAVITY;
        else if (!strcmp(type, "gravity_time")) s.type = STAGE_GRAVITY_TIME;
        else s.type = STAGE_TEMPERATURE;

        count++;
    }

    fermentacaoState.totalStages = count;

    if (count > 0 &&
        fermentacaoState.currentStageIndex < count) {
        updateTargetTemperature(
            fermentacaoState.stages[
                fermentacaoState.currentStageIndex
            ].targetTemp
        );
    }
    fermentacaoState.totalStages = count;
    sendStageTimers(); // Envia os tempos assim que a configuração é baixada
}

// =====================================================
// TROCA DE FASE
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    if (fermentacaoState.currentStageIndex >=
        fermentacaoState.totalStages) return;

    FermentationStage& stage =
        fermentacaoState.stages[
            fermentacaoState.currentStageIndex
        ];

    unsigned long now = millis();

    if (!stageStarted) {
        stageStartTime = now;
        stageStarted = true;
        targetReachedSent = false; 
        saveStateToEEPROM();
    }

    float elapsedH =
        (now - stageStartTime) / 3600000.0;
    float elapsedD =
        (now - stageStartTime) / 86400000.0;

    if (processCurrentStage(stage, elapsedD, elapsedH)) {

        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        stageStartTime = 0;

        if (fermentacaoState.currentStageIndex <
            fermentacaoState.totalStages) {

            FermentationStage& next =
                fermentacaoState.stages[
                    fermentacaoState.currentStageIndex
                ];

            updateTargetTemperature(next.targetTemp);
            saveStateToEEPROM();
        } else {
            Serial.println(F("[Fase] 🎉 Concluída"));
            deactivateCurrentFermentation();
        }
    }
}

void verificarTargetAtingido() {
    // Só verifica se a fermentação está ativa e se ainda não enviamos nesta fase
    if (!fermentacaoState.active || targetReachedSent) return;

    // Calcula a diferença entre temperatura atual e alvo [2, 3]
    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) { // Tolerância de 0.5°C [2]
        JsonDocument doc;
        doc["targetReachedTime"][".sv"] = "timestamp"; // Timestamp do servidor Firebase
        
        // Envia para o nó fermentationState
        String payload;
        serializeJson(doc, payload);
        Database.update(aClient, "/fermentationState", payload);

        targetReachedSent = true; // Impede envios duplicados na mesma fase
        Serial.println(F("[Firebase] 🎯 Temperatura alvo atingida!"));
    }
}

// =====================================================
// LEITURAS
// =====================================================
void enviarLeituraAtual() {
    if (!fermentacaoState.active ||
        strlen(fermentacaoState.activeId) == 0) return;

    JsonDocument doc;
    doc["tempFridge"]     = sensors.getTempCByIndex(1);
    doc["tempFermenter"]  = state.currentTemp;
    doc["gravity"]        = mySpindel.gravity;
    doc["tempTarget"]     = fermentacaoState.tempTarget;
    doc["timestamp"][".sv"] = "timestamp";

    String path =
        String("/readings/") +
        fermentacaoState.activeId;

    String payload;
    serializeJson(doc, payload);
    Database.push(aClient, path.c_str(), payload);
}
