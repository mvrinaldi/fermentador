// config_cache.h - Cache LittleFS da configuração de etapas
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "estruturas.h"
#include "globais.h"
#include "debug_config.h"

// Forward declaration — definida em controle_fermentacao.cpp
void updateTargetTemperature(float newTemp);

#define CONFIG_CACHE_PATH "/config_cache.json"

// Salva o JSON bruto da configuração recebida do servidor
inline bool saveConfigCache(const char* configId, JsonDocument& doc) {
    File f = LittleFS.open(CONFIG_CACHE_PATH, "w");
    if (!f) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Cache] ❌ Falha ao abrir arquivo para escrita"));
        #endif
        return false;
    }

    // Adiciona o ID ao documento antes de salvar
    doc["_cachedId"] = configId;

    size_t written = serializeJson(doc, f);
    f.close();

    #if DEBUG_FERMENTATION
    Serial.printf("[Cache] ✅ Config salva: %u bytes (ID: %s)\n", written, configId);
    #endif

    return written > 0;
}

// Carrega o cache e aplica ao fermentacaoState
// Retorna true se carregou com sucesso E o ID bate com o activeId atual
inline bool loadConfigCache(const char* expectedId) {
    if (!LittleFS.exists(CONFIG_CACHE_PATH)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Cache] Arquivo não encontrado"));
        #endif
        return false;
    }

    File f = LittleFS.open(CONFIG_CACHE_PATH, "r");
    if (!f) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Cache] ❌ Falha ao abrir arquivo para leitura"));
        #endif
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        #if DEBUG_FERMENTATION
        Serial.printf("[Cache] ❌ JSON inválido: %s\n", err.c_str());
        #endif
        LittleFS.remove(CONFIG_CACHE_PATH);
        return false;
    }

    // Valida que o cache é para a fermentação correta
    const char* cachedId = doc["_cachedId"] | "";
    if (strcmp(cachedId, expectedId) != 0) {
        #if DEBUG_FERMENTATION
        Serial.printf("[Cache] ⚠️  ID diverge: cache='%s', esperado='%s'\n",
                      cachedId, expectedId);
        #endif
        return false;
    }

    // --- Aplica a configuração (mesma lógica do loadConfigParameters) ---

    const char* name = doc["name"] | "Sem nome";
    fermentacaoState.setConfigName(name);

    // NÃO sobrescreve currentStageIndex — já foi restaurado do Preferences
    // Só recarrega totalStages e stages[]

    JsonArray stages = doc["stages"];
    int count = 0;

    for (JsonVariant stageVar : stages) {
        if (count >= MAX_STAGES) break;

        JsonObject stage = stageVar.as<JsonObject>();
        FermentationStage& s = fermentacaoState.stages[count];

        const char* type = stage["type"] | "temperature";
        if (strcmp(type, "ramp") == 0)               s.type = STAGE_RAMP;
        else if (strcmp(type, "gravity") == 0)        s.type = STAGE_GRAVITY;
        else if (strcmp(type, "gravity_time") == 0)   s.type = STAGE_GRAVITY_TIME;
        else                                           s.type = STAGE_TEMPERATURE;

        s.targetTemp    = stage["targetTemp"]    | 20.0f;
        s.startTemp     = stage["startTemp"]     | 20.0f;
        s.rampTimeHours = (int)(stage["rampTime"] | 0.0f);
        s.durationDays  = stage["duration"]      | 0.0f;
        s.targetGravity = stage["targetGravity"] | 0.0f;
        s.timeoutDays   = stage["timeoutDays"]   | 0.0f;

        s.holdTimeHours = s.durationDays * 24.0f;
        s.maxTimeHours  = s.timeoutDays  * 24.0f;

        s.startTime = 0;
        s.completed = false;

        count++;
    }

    fermentacaoState.totalStages = count;

    // Aplica temperatura da etapa atual
    if (count > 0 && fermentacaoState.currentStageIndex < count) {
        FermentationStage& cur = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        float t = (cur.type == STAGE_RAMP) ? cur.startTemp : cur.targetTemp;
        updateTargetTemperature(t);
    }

    #if DEBUG_FERMENTATION
    Serial.printf("[Cache] ✅ Restaurado do LittleFS: %d etapas, etapa atual %d\n",
                  count, fermentacaoState.currentStageIndex + 1);
    #endif

    return count > 0;
}

inline void clearConfigCache() {
    if (LittleFS.exists(CONFIG_CACHE_PATH)) {
        LittleFS.remove(CONFIG_CACHE_PATH);
        #if DEBUG_FERMENTATION
        Serial.println(F("[Cache] 🗑️  Cache removido"));
        #endif
    }
}