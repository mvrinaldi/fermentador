// controle_fermentacao.cpp - Reescrito para integração BrewPi
// ✅ CORRIGIDO: Lógica de timeRemaining - só envia quando targetReached = true
// ✅ REFATORADO: Lógica de envio MySQL movida para mysql_sender.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <cstring>
#include <time.h>

#include "definitions.h"
#include "estruturas.h"
#include "globais.h"
#include "http_client.h"
#include "BrewPiStructs.h"
#include "BrewPiTempControl.h"
#include "controle_fermentacao.h"
#include "eeprom_layout.h"
#include "fermentacao_stages.h"
#include "gerenciador_sensores.h"
#include "debug_config.h"
#include "message_codes.h"
#include "mysql_sender.h"  // ✅ NOVO: Módulo de envio MySQL

extern FermentadorHTTPClient httpClient;

// =====================================================
// VARIÁVEIS DE CONTROLE
// =====================================================
unsigned long lastActiveCheck = 0;
char lastActiveId[64] = "";
bool isFirstCheck = true;
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

bool isValidString(const char* str) {
    return str && str[0] != '\0';
}

// ✅ NOVO: Função auxiliar para ler float do JSON (trata string e número)
static float jsonToFloat(JsonVariant value, float defaultValue = 0.0f) {
    if (value.isNull()) {
        return defaultValue;
    }
    if (value.is<float>()) {
        return value.as<float>();
    }
    if (value.is<int>()) {
        return (float)value.as<int>();
    }
    if (value.is<const char*>()) {
        const char* str = value.as<const char*>();
        if (str && str[0] != '\0') {
            return atof(str);
        }
    }
    return defaultValue;
}

// =====================================================
// FUNÇÕES DE TEMPO
// =====================================================

String formatTime(time_t timestamp) {
    if (timestamp == 0) return "INVALID";
    
    struct tm timeinfo;
    gmtime_r(&timestamp, &timeinfo);
    
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S UTC", &timeinfo);
    return String(buffer);
}

time_t getCurrentEpoch() {
    static time_t lastValidEpoch = 0;
    static unsigned long lastValidMillis = 0;
    static bool epochInitialized = false;
    
    time_t now = time(nullptr);
    
    if (now < 1577836800L) {
        if (!epochInitialized) {
            EEPROM.begin(EEPROM_SIZE);
            EEPROM.get(ADDR_LAST_VALID_EPOCH, lastValidEpoch);
            EEPROM.get(ADDR_LAST_VALID_MILLIS, lastValidMillis);
            epochInitialized = true;

            #if DEBUG_FERMENTATION
            if (lastValidEpoch > 1577836800L) {
                Serial.print(F("[NTP] ⚠️  Usando backup EEPROM: "));
                Serial.println(formatTime(lastValidEpoch));
            }
            #endif
        }
        
        if (lastValidEpoch > 1577836800L) {
            return lastValidEpoch + ((millis() - lastValidMillis) / 1000);
        }
        
        #if DEBUG_FERMENTATION
        Serial.println(F("[NTP] ⚠️  Relógio não sincronizado!"));
        #endif

        return 0;
    }
    
    static unsigned long lastBackup = 0;
    if (millis() - lastBackup > 60000) {
        lastValidEpoch = now;
        lastValidMillis = millis();
        
        EEPROM.begin(EEPROM_SIZE);
        EEPROM.put(ADDR_LAST_VALID_EPOCH, lastValidEpoch);
        EEPROM.put(ADDR_LAST_VALID_MILLIS, lastValidMillis);
        EEPROM.commit();
        lastBackup = millis();
    }
    
    return now;
}

// =====================================================
// EEPROM
// =====================================================
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    eepromWriteString(fermentacaoState.activeId, ADDR_ACTIVE_ID, sizeof(fermentacaoState.activeId));
    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    time_t epoch = fermentacaoState.stageStartEpoch;
    EEPROM.put(ADDR_STAGE_START_TIME, epoch);

    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);
    
    // ✅ NOVO: Salvar targetReachedSent na EEPROM
    EEPROM.put(ADDR_TARGET_REACHED_FLAG, fermentacaoState.targetReachedSent);
    
    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    
    #if DEBUG_FERMENTATION
    if (!EEPROM.commit()) {
        Serial.println(F("[EEPROM] ❌ Falha ao salvar estado"));
    } else {
        Serial.print(F("[EEPROM] ✅ Estado salvo (início: "));
        Serial.print(formatTime(epoch));
        Serial.printf(", targetReached: %s)\n", 
                     fermentacaoState.targetReachedSent ? "true" : "false");
    }
    #else
    EEPROM.commit();
    #endif    
}

void loadStateFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    if (EEPROM.read(ADDR_CONFIG_SAVED) != 1) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[EEPROM] Nenhum estado salvo"));
        #endif
        return;
    }

    eepromReadString(fermentacaoState.activeId, 
                     sizeof(fermentacaoState.activeId), 
                     ADDR_ACTIVE_ID, 
                     sizeof(fermentacaoState.activeId));

    if (!isValidString(fermentacaoState.activeId)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[EEPROM] ⚠️  ID inválido, limpando..."));
        #endif
        clearEEPROM();
        fermentacaoState.clear();
        return;
    }

    EEPROM.get(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    time_t savedEpoch;
    EEPROM.get(ADDR_STAGE_START_TIME, savedEpoch);
    fermentacaoState.stageStartEpoch = savedEpoch;

    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);
    
    // ✅ NOVO: Restaurar targetReachedSent da EEPROM
    EEPROM.get(ADDR_TARGET_REACHED_FLAG, fermentacaoState.targetReachedSent);

    fermentacaoState.active = isValidString(fermentacaoState.activeId);

    if (fermentacaoState.active && !isValidString(fermentacaoState.activeId)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[EEPROM] ⚠️  Estado inconsistente, limpando..."));
        #endif
        clearEEPROM();
        fermentacaoState.clear();
        fermentacaoState.tempTarget = 20.0;
        state.targetTemp = 20.0;
        return;
    }

    #if DEBUG_FERMENTATION
    Serial.print(F("[EEPROM] ✅ Estado restaurado: ID="));
    Serial.print(fermentacaoState.activeId);
    Serial.print(", início=");
    Serial.print(formatTime(savedEpoch));
    Serial.printf(", targetReached=%s\n", 
                 fermentacaoState.targetReachedSent ? "true" : "false");
    #endif
}

void clearEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    for (int i = ADDR_FERMENTATION_START; i <= 127; i++) {
        EEPROM.write(i, 0);
    }
    
    #if DEBUG_FERMENTATION
    if (EEPROM.commit()) {
        Serial.println(F("[EEPROM] ✅ Seção de fermentação limpa"));
    } else {
        Serial.println(F("[EEPROM] ❌ Falha ao limpar EEPROM"));
    }
    #else
    EEPROM.commit();
    #endif
}

// =====================================================
// CONTROLE DE TEMPERATURA - INTEGRAÇÃO BREWPI
// =====================================================

void updateTargetTemperature(float newTemp) {
    temperature temp = floatToTemp(newTemp);
    brewPiControl.setBeerTemp(temp);
    
    fermentacaoState.tempTarget = newTemp;
    state.targetTemp = newTemp;
    
    #if DEBUG_FERMENTATION
    Serial.printf("[BrewPi] 🎯 Novo alvo: %.2f°C\n", newTemp);
    #endif
}

float getCurrentBeerTemp() {
    temperature temp = brewPiControl.getBeerTemp();
    float tempFloat = tempToFloat(temp);
    
    state.currentTemp = tempFloat;
    
    return tempFloat;
}

void resetPIDState() {
    brewPiControl.reset();

    #if DEBUG_FERMENTATION
    Serial.println(F("[BrewPi] ✅ Estado do controle resetado"));
    #endif
}

// =====================================================
// CONTROLE DE ESTADO
// =====================================================

void concluirFermentacaoMantendoTemperatura() {
    #if DEBUG_FERMENTATION
    Serial.println(F("[Fase] ✅ Fermentação concluída - mantendo temperatura atual"));
    #endif
    
    // ✅ NOVO: Notifica servidor que a última etapa foi concluída (como nas demais)
    if (httpClient.isConnected()) {
        // Envia atualização de índice para totalStages (indica conclusão)
        httpClient.updateStageIndex(fermentacaoState.activeId, fermentacaoState.totalStages);
    }
    
    // Envia estado de conclusão
    JsonDocument doc;
    doc["s"] = MSG_CHOLD;
    time_t completionEpoch = getCurrentEpoch();
    if (completionEpoch > 0) {
        doc["ca"] = completionEpoch;
    }
    doc["msg"] = MSG_FCONC;
    doc["cid"] = fermentacaoState.activeId;
    
    // ✅ NOVO: Envia timeRemaining com código "tc" (time completed)
    JsonArray trArray = doc["tr"].to<JsonArray>();
    trArray.add(MSG_TC);  // "tc" = fermentação concluída
    
    if (httpClient.isConnected()) {
        httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    }
    
    fermentacaoState.concluidaMantendoTemp = true;
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[Fase] 🌡️  Sistema mantém temperatura atual até comando manual"));
    Serial.printf("[Fase] 🔒 Temperatura mantida: %.1f°C\n", fermentacaoState.tempTarget);
    #endif
}

void deactivateCurrentFermentation() {
    #if DEBUG_FERMENTATION
    Serial.println(F("[MySQL] 🧹 Desativando fermentação"));
    #endif

    brewPiControl.reset();
    
    fermentacaoState.activeId[0] = '\0';
    lastActiveId[0] = '\0';

    fermentacaoState.active = false;
    fermentacaoState.concluidaMantendoTemp = false;
    fermentacaoState.currentStageIndex = 0;
    fermentacaoState.totalStages = 0;
    fermentacaoState.stageStartEpoch = 0;
    fermentacaoState.targetReachedSent = false;
    stageStarted = false;

    updateTargetTemperature(DEFAULT_TEMPERATURE);
    clearEEPROM();
    saveStateToEEPROM();
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[BrewPi] ✅ Sistema resetado na desativação"));
    #endif
}

void setupActiveListener() {
    #if DEBUG_FERMENTATION
    Serial.println(F("[MySQL] Sistema inicializado"));
    #endif

    loadStateFromEEPROM();
    
    // ✅ NOVO: Validar consistência do estado restaurado
    if (fermentacaoState.active) {
        // Se targetReachedSent é true mas stageStartEpoch é 0, há inconsistência
        if (fermentacaoState.targetReachedSent && fermentacaoState.stageStartEpoch == 0) {
            #if DEBUG_FERMENTATION
            Serial.println(F("[EEPROM] ⚠️ Estado inconsistente detectado!"));
            Serial.println(F("[EEPROM] targetReachedSent=true mas stageStartEpoch=0"));
            Serial.println(F("[EEPROM] Resetando targetReachedSent para false"));
            #endif
            
            // Reset para forçar nova detecção de temperatura atingida
            fermentacaoState.targetReachedSent = false;
            saveStateToEEPROM();
        }
    }
    
    brewPiControl.reset();

    #if DEBUG_FERMENTATION
    Serial.println(F("[BrewPi] ✅ Sistema resetado na inicialização"));
    #endif
}

// =====================================================
// VERIFICAÇÃO DE COMANDOS DO SITE
// =====================================================
void checkPauseOrComplete() {
    if (!fermentacaoState.active) return;
    if (!httpClient.isConnected()) return;
    
    JsonDocument doc;
    
    if (!httpClient.getConfiguration(fermentacaoState.activeId, doc)) {
        return;
    }
    
    const char* status = doc["status"] | "active";
    
    if (strcmp(status, "paused") == 0) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ⏸️  Fermentação PAUSADA pelo site"));
        #endif
        deactivateCurrentFermentation();
    } else if (strcmp(status, "completed") == 0) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ✅ Fermentação CONCLUÍDA pelo site"));
        #endif
        
        if (fermentacaoState.concluidaMantendoTemp) {
            #if DEBUG_FERMENTATION
            Serial.println(F("[MySQL] 🧹 Finalizando manutenção de temperatura por comando do site"));
            #endif
            deactivateCurrentFermentation();
        } else {
            concluirFermentacaoMantendoTemperatura();
        }
    }
}

void getTargetFermentacao() {
    unsigned long now = millis();

    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_FERMENTATION(F("[MySQL] ⚠️ WiFi desconectado"));
        isFirstCheck = false;
        return;
    }

    LOG_FERMENTATION(F("\n========================================"));
    LOG_FERMENTATION(F("[MySQL] 🔍 INICIANDO BUSCA DE FERMENTAÇÃO"));
    LOG_FERMENTATION(F("========================================"));

    JsonDocument doc;
    
    bool requestOk = httpClient.getActiveFermentation(doc);
    
    LOG_FERMENTATION("[MySQL] getActiveFermentation() retornou: " + String(requestOk ? "TRUE" : "FALSE"));
    
    if (!requestOk) {
        LOG_FERMENTATION(F("[MySQL] ❌ Falha na requisição HTTP"));
        isFirstCheck = false;
        return;
    }

    #if DEBUG_FERMENTATION
    Serial.println(F("\n[MySQL] 📄 DOCUMENTO JSON RECEBIDO:"));
    serializeJsonPretty(doc, Serial);
    Serial.println();
    #endif
    
    bool active = doc["active"] | false;
    
    String idString;
    if (doc["id"].is<int>()) {
        idString = String(doc["id"].as<int>());
    } else if (doc["id"].is<const char*>()) {
        idString = doc["id"].as<const char*>();
    }
    
    const char* id = idString.c_str();
    int serverStageIndex = doc["currentStageIndex"] | 0;
    
    LOG_FERMENTATION(F("\n[MySQL] 🔍 VALORES EXTRAÍDOS:"));
    LOG_FERMENTATION("  active: " + String(active ? "TRUE" : "FALSE"));
    LOG_FERMENTATION("  id: '" + String(id) + "' (length: " + String(strlen(id)) + ")");
    LOG_FERMENTATION("  serverStageIndex: " + String(serverStageIndex));
    
    LOG_FERMENTATION(F("\n[MySQL] 🔍 ESTADO ATUAL DO SISTEMA:"));
    LOG_FERMENTATION("  fermentacaoState.active: " + String(fermentacaoState.active ? "TRUE" : "FALSE"));
    LOG_FERMENTATION("  fermentacaoState.activeId: '" + String(fermentacaoState.activeId) + "'");
    LOG_FERMENTATION("  fermentacaoState.currentStageIndex: " + String(fermentacaoState.currentStageIndex));
    LOG_FERMENTATION("  lastActiveId: '" + String(lastActiveId) + "'");

    if (!isValidString(id)) {
        LOG_FERMENTATION(F("[MySQL] ⚠️ ID é inválido ou vazio!"));
        id = "";
    } else {
        LOG_FERMENTATION("[MySQL] ✅ ID válido: '" + String(id) + "'");
    }

    LOG_FERMENTATION(F("\n[MySQL] 🔍 DECISÃO:"));

    if (active && isValidString(id)) {
        LOG_FERMENTATION(F("  → Fermentação ATIVA detectada no servidor"));
        
        if (strcmp(id, lastActiveId) != 0) {
            // =====================================================
            // NOVA FERMENTAÇÃO DETECTADA
            // =====================================================
            LOG_FERMENTATION(F("  → ID DIFERENTE do último conhecido"));
            LOG_FERMENTATION("     Anterior: '" + String(lastActiveId) + "'");
            LOG_FERMENTATION("     Novo:     '" + String(id) + "'");
            LOG_FERMENTATION(F("  → INICIANDO NOVA FERMENTAÇÃO"));

            brewPiControl.reset();
            LOG_FERMENTATION(F("[BrewPi] ✅ Sistema resetado para nova fermentação"));
            
            fermentacaoState.active = true;
            fermentacaoState.concluidaMantendoTemp = false;
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            fermentacaoState.currentStageIndex = serverStageIndex;
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            LOG_FERMENTATION("[MySQL] 🔧 Carregando configuração ID: " + String(id));

            loadConfigParameters(id);

            stageStarted = false;
            fermentacaoState.targetReachedSent = false;
            fermentacaoState.stageStartEpoch = 0;

            saveStateToEEPROM();
            
            LOG_FERMENTATION(F("[MySQL] ✅ CONFIGURAÇÃO CONCLUÍDA"));
            LOG_FERMENTATION("  activeId: '" + String(fermentacaoState.activeId) + "'");
            LOG_FERMENTATION("  tempTarget: " + String(fermentacaoState.tempTarget, 1) + "°C");
            LOG_FERMENTATION("  totalStages: " + String(fermentacaoState.totalStages));
        } else {
            // =====================================================
            // MESMA FERMENTAÇÃO - VERIFICAR SINCRONIZAÇÃO DE ETAPA
            // =====================================================
            LOG_FERMENTATION(F("  → MESMO ID do último conhecido"));
            LOG_FERMENTATION(F("  → Fermentação já configurada"));
            
            if (serverStageIndex != fermentacaoState.currentStageIndex) {
                LOG_FERMENTATION(F("  → Diferença de etapa detectada!"));
                LOG_FERMENTATION("     Local:    " + String(fermentacaoState.currentStageIndex));
                LOG_FERMENTATION("     Servidor: " + String(serverStageIndex));
                
                // ✅ CORREÇÃO: Só aceita índice do servidor se for MAIOR que o local
                if (serverStageIndex > fermentacaoState.currentStageIndex) {
                    LOG_FERMENTATION(F("  → Servidor à frente - aceitando mudança externa"));
                    
                    fermentacaoState.currentStageIndex = serverStageIndex;
                    stageStarted = false;
                    fermentacaoState.stageStartEpoch = 0;
                    fermentacaoState.targetReachedSent = false;
                            
                    brewPiControl.reset();
                    saveStateToEEPROM();
                    
                    LOG_FERMENTATION("  → Etapa atualizada para " + String(serverStageIndex));
                } else {
                    // Local está à frente do servidor - servidor desatualizado
                    LOG_FERMENTATION(F("  → Local à frente - servidor desatualizado"));
                    LOG_FERMENTATION(F("  → Mantendo estado local e notificando servidor"));
                    
                    // ✅ Tenta atualizar o servidor com o índice correto
                    if (httpClient.isConnected()) {
                        bool updated = httpClient.updateStageIndex(
                            fermentacaoState.activeId, 
                            fermentacaoState.currentStageIndex
                        );
                        
                        if (updated) {
                            LOG_FERMENTATION("  → Servidor sincronizado para etapa " + String(fermentacaoState.currentStageIndex));
                        } else {
                            LOG_FERMENTATION(F("  → Falha ao sincronizar servidor (tentará novamente)"));
                        }
                    }
                }
            }
        }
    } else if (fermentacaoState.active && !active) {
        // =====================================================
        // FERMENTAÇÃO LOCAL ATIVA, SERVIDOR INATIVO
        // =====================================================
        if (fermentacaoState.concluidaMantendoTemp) {
            LOG_FERMENTATION(F("  → Concluída localmente, mantendo temperatura (servidor offline)"));
        } else {
            LOG_FERMENTATION(F("  → Fermentação estava ativa LOCALMENTE"));
            LOG_FERMENTATION(F("  → Servidor indica NÃO ATIVA"));
            LOG_FERMENTATION(F("  → DESATIVANDO"));
            deactivateCurrentFermentation();
        }
    } else if (!active && !fermentacaoState.active) {
        // =====================================================
        // NENHUMA FERMENTAÇÃO ATIVA
        // =====================================================
        LOG_FERMENTATION(F("  → Nenhuma fermentação ativa"));
        LOG_FERMENTATION(F("  → Sistema em STANDBY"));
        
        if (state.targetTemp == DEFAULT_TEMPERATURE) {
            brewPiControl.reset();
            LOG_FERMENTATION(F("[BrewPi] ✅ Sistema resetado em modo standby"));
        }
    } else if (!active && fermentacaoState.active) {
        // =====================================================
        // SERVIDOR OFFLINE MAS TEMOS ESTADO LOCAL
        // =====================================================
        LOG_FERMENTATION(F("  → Servidor offline mas temos estado local"));
        LOG_FERMENTATION(F("  → MANTENDO fermentação local"));
    }

    LOG_FERMENTATION(F("========================================"));
    LOG_FERMENTATION(F("[MySQL] FIM DA VERIFICAÇÃO"));
    LOG_FERMENTATION(F("========================================\n"));
    
    isFirstCheck = false;
}

// =====================================================
// CONFIGURAÇÃO DE ETAPAS
// =====================================================
void loadConfigParameters(const char* configId) {
    if (!configId || strlen(configId) == 0) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ❌ ID inválido"));
        #endif
        return;
    }

    LOG_FERMENTATION("[MySQL] 🔧 Buscando config: " + String(configId));
    
    JsonDocument doc;
    
    if (!httpClient.getConfiguration(configId, doc)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ❌ Falha ao buscar configuração"));
        #endif
        return;
    }

    fermentacaoState.currentStageIndex = doc["currentStageIndex"] | 0;
    
    const char* name = doc["name"] | "Sem nome";
    fermentacaoState.setConfigName(name);
    
    JsonArray stages = doc["stages"];
    int count = 0;
    
    for (JsonVariant stageVar : stages) {
        if (count >= MAX_STAGES) {
            #if DEBUG_FERMENTATION
            Serial.println(F("[MySQL] ⚠️  Máximo de etapas excedido"));
            #endif
            break;
        }

        JsonObject stage = stageVar.as<JsonObject>();
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

        // ✅ CORRIGIDO: Usa jsonToFloat para tratar DECIMAL como string do MySQL
        s.targetTemp = jsonToFloat(stage["targetTemp"], 20.0f);
        s.startTemp = jsonToFloat(stage["startTemp"], 20.0f);
        s.rampTimeHours = (int)jsonToFloat(stage["rampTime"], 0.0f);
        s.durationDays = jsonToFloat(stage["duration"], 0.0f);
        s.targetGravity = jsonToFloat(stage["targetGravity"], 0.0f);
        s.timeoutDays = jsonToFloat(stage["timeoutDays"], 0.0f);
        
        // Campos calculados
        s.holdTimeHours = s.durationDays * 24.0f;
        s.maxTimeHours = s.timeoutDays * 24.0f;
        
        s.startTime = 0;
        s.completed = false;

        #if DEBUG_FERMENTATION
        Serial.printf("[MySQL] Etapa %d: tipo=%s, temp=%.1f, duração=%.2f dias (%.1f horas)\n",
                     count, type, s.targetTemp, s.durationDays, s.holdTimeHours);
        #endif

        count++;
    }

    fermentacaoState.totalStages = count;

    if (count > 0 && fermentacaoState.currentStageIndex < count) {
        float targetTemp = fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp;
        updateTargetTemperature(targetTemp);

        #if DEBUG_FERMENTATION
        Serial.printf("[MySQL] 🌡️  Temperatura alvo: %.1f°C\n", targetTemp);
        #endif
    }

    #if DEBUG_FERMENTATION
    Serial.printf("[MySQL] ✅ Configuração carregada: %d etapas\n", count);
    #endif
}

// =====================================================
// ✅ TROCA DE FASE
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    
    // ✅ CORREÇÃO: Se fermentação concluída mantendo temp, não processar mais etapas
    if (fermentacaoState.concluidaMantendoTemp) {
        #if DEBUG_FERMENTATION
        static unsigned long lastHoldDebug = 0;
        if (millis() - lastHoldDebug > 300000) {  // Log a cada 5 minutos
            lastHoldDebug = millis();
            Serial.printf("[Fase] 🔒 Mantendo temperatura %.1f°C (aguardando comando manual)\n",
                         fermentacaoState.tempTarget);
        }
        #endif
        return;  // Não faz mais nada - apenas mantém temperatura
    }
    
    #if DEBUG_FERMENTATION
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 30000) {
        lastDebug = millis();
        Serial.println(F("\n╔════════════════════════════════════╗"));
        Serial.println(F("║   DEBUG verificarTrocaDeFase()     ║"));
        Serial.println(F("╠════════════════════════════════════╣"));
        Serial.printf("║ stageStarted:     %s               ║\n", 
                     stageStarted ? "TRUE " : "FALSE");
        Serial.printf("║ PID atual:        %6.1f°C          ║\n", 
                     fermentacaoState.tempTarget);
        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            Serial.printf("║ Alvo etapa:       %6.1f°C          ║\n", 
                         fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp);
        }
        Serial.println(F("╚════════════════════════════════════╝\n"));
    }
    #endif
    
    if (fermentacaoState.totalStages == 0) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Fase] ⚠️  0 etapas, desativando..."));
        #endif
        deactivateCurrentFermentation();
        return;
    }
    
    // ✅ CORREÇÃO: Se índice >= total, significa que todas as etapas foram concluídas
    // NÃO deve resetar para 0, deve concluir mantendo temperatura
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Fase] ℹ️  Todas as etapas concluídas"));
        #endif
        
        // Se ainda não está no modo de manutenção, ativa
        if (!fermentacaoState.concluidaMantendoTemp) {
            concluirFermentacaoMantendoTemperatura();
        }
        return;
    }

    FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
    
    time_t nowEpoch = getCurrentEpoch();
    
    if (nowEpoch == 0) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Fase] ⚠️ Aguardando sincronização NTP..."));
        #endif
        return;
    }
    
    // =====================================================
    // INÍCIO DE NOVA ETAPA
    // =====================================================
    if (!stageStarted) {
        stageStarted = true;
        fermentacaoState.targetReachedSent = false;
        fermentacaoState.stageStartEpoch = 0;
        
        brewPiControl.reset();

        #if DEBUG_FERMENTATION
        Serial.println(F("[BrewPi] ✅ Sistema resetado para nova etapa"));
        #endif
        
        float newTargetTemp;
        if (stage.type == STAGE_RAMP) {
            newTargetTemp = stage.startTemp;
        } else {
            newTargetTemp = stage.targetTemp;
        }
        
        updateTargetTemperature(newTargetTemp);
        saveStateToEEPROM();
        
        #if DEBUG_FERMENTATION
        Serial.printf("[Fase] ▶️  Etapa %d/%d iniciada - Alvo: %.1f°C (tipo: ", 
                    fermentacaoState.currentStageIndex + 1,
                    fermentacaoState.totalStages,
                    newTargetTemp);
                    
        switch (stage.type) {
            case STAGE_TEMPERATURE:
                Serial.printf("TEMPERATURE, duração: %.2f dias / %.1f horas)\n", 
                             stage.durationDays, stage.holdTimeHours);
                break;
            case STAGE_RAMP:
                Serial.printf("RAMP, tempo: %d horas)\n", stage.rampTimeHours);
                break;
            case STAGE_GRAVITY:
                Serial.printf("GRAVITY, alvo: %.3f)\n", stage.targetGravity);
                break;
            case STAGE_GRAVITY_TIME:
                Serial.printf("GRAVITY_TIME, timeout: %.2f dias / %.1f horas)\n", 
                             stage.timeoutDays, stage.maxTimeHours);
                break;
        }
        #endif
    }

    // =====================================================
    // VERIFICAÇÃO DE TEMPERATURA ALVO ATINGIDA
    // =====================================================
    bool targetReached = false;
    bool needsTemperature = (stage.type == STAGE_TEMPERATURE || 
                            stage.type == STAGE_GRAVITY || 
                            stage.type == STAGE_GRAVITY_TIME);

    if (needsTemperature) {
        float currentTemp = getCurrentBeerTemp();
        float stageTargetTemp = stage.targetTemp;
        float diff = abs(currentTemp - stageTargetTemp);
        targetReached = (diff <= TEMPERATURE_TOLERANCE);
        
        #if DEBUG_FERMENTATION
        static unsigned long lastDebug2 = 0;
        unsigned long now = millis();
        if (now - lastDebug2 > 60000 && !fermentacaoState.targetReachedSent) {
            lastDebug2 = now;
            Serial.printf("[Fase] Aguardando alvo: Temp=%.1f°C, Alvo=%.1f°C, Diff=%.1f°C, Atingiu=%s\n",
                         currentTemp, stageTargetTemp, diff, targetReached ? "SIM" : "NÃO");
        }
        #endif
        
        // ✅ CORRIGIDO: Lógica de definição de targetReachedSent e stageStartEpoch
        if (targetReached) {
            // Caso 1: Primeira vez atingindo o alvo nesta etapa
            if (!fermentacaoState.targetReachedSent) {
                fermentacaoState.targetReachedSent = true;
                
                if (fermentacaoState.stageStartEpoch == 0) {
                    fermentacaoState.stageStartEpoch = nowEpoch;
                }
                
                saveStateToEEPROM();
                
                #if DEBUG_FERMENTATION
                Serial.printf("[Fase] 🎯 Temperatura FINAL da etapa atingida: %.1f°C!\n", stageTargetTemp);
                Serial.printf("[Fase] ⏱️  Contagem iniciada em: %s\n", formatTime(fermentacaoState.stageStartEpoch).c_str());
                #endif
            }
            // Caso 2: targetReachedSent já é true (restaurado da EEPROM), mas stageStartEpoch é 0
            // Isso NÃO deveria acontecer após a correção, mas é uma recuperação de segurança
            else if (fermentacaoState.stageStartEpoch == 0) {
                #if DEBUG_FERMENTATION
                Serial.println(F("[Fase] ⚠️ RECUPERAÇÃO: targetReachedSent=true mas stageStartEpoch=0"));
                Serial.println(F("[Fase] ⚠️ Definindo stageStartEpoch com timestamp atual"));
                #endif
                
                fermentacaoState.stageStartEpoch = nowEpoch;
                saveStateToEEPROM();
            }
        }
    } 
    else if (stage.type == STAGE_RAMP) {
        targetReached = true;
        
        if (fermentacaoState.stageStartEpoch == 0) {
            fermentacaoState.stageStartEpoch = nowEpoch;
            saveStateToEEPROM();
            
            #if DEBUG_FERMENTATION
            Serial.println(F("[Fase] ⏱️  Contagem de rampa iniciada"));
            #endif
        }
    }

    // =====================================================
    // CÁLCULO DO TEMPO DECORRIDO
    // =====================================================
    float elapsedH = 0;
    
    if (fermentacaoState.stageStartEpoch > 0) {
        elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0f;
        if (elapsedH < 0) elapsedH = 0;
    }
    
    #if DEBUG_FERMENTATION
    static unsigned long lastDebug3 = 0;
    if (millis() - lastDebug3 > 300000) {
        lastDebug3 = millis();
        
        Serial.printf("[Fase] Etapa %d/%d: ", 
                     fermentacaoState.currentStageIndex + 1,
                     fermentacaoState.totalStages);
        
        if (fermentacaoState.stageStartEpoch > 0) {
            Serial.printf("%.1fh decorridas / ", elapsedH);
            
            switch (stage.type) {
                case STAGE_TEMPERATURE:
                    Serial.printf("%.1fh total (%.2f dias)", 
                                 stage.holdTimeHours, stage.durationDays);
                    break;
                case STAGE_GRAVITY:
                case STAGE_GRAVITY_TIME:
                    Serial.printf("%.1fh max (%.2f dias)", 
                                 stage.maxTimeHours, stage.timeoutDays);
                    break;
                case STAGE_RAMP:
                    Serial.printf("%dh total", stage.rampTimeHours);
                    break;
            }
        } else {
            Serial.print("Aguardando temperatura alvo");
        }
        
        Serial.printf(" (targetReached: %s)\n", targetReached ? "SIM" : "NÃO");
    }
    #endif

    // =====================================================
    // CONTROLE DE RAMPA
    // =====================================================
    if (stage.type == STAGE_RAMP) {
        float progress = elapsedH / (float)stage.rampTimeHours;
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;

        float temp = stage.startTemp + (stage.targetTemp - stage.startTemp) * progress;
        updateTargetTemperature(temp);
        
        #if DEBUG_FERMENTATION
        static unsigned long lastRampDebug = 0;
        if (millis() - lastRampDebug > 60000) {
            lastRampDebug = millis();
            Serial.printf("[Rampa Etapa] Progresso: %.1f°C (%.0f%%)\n", 
                         temp, progress * 100.0f);
        }
        #endif
    }

    // =====================================================
    // VERIFICAÇÃO DE CONCLUSÃO DA ETAPA
    // =====================================================
    bool stageCompleted = false;

    switch (stage.type) {
        case STAGE_TEMPERATURE:
            if (targetReached && fermentacaoState.stageStartEpoch > 0) {
                if (elapsedH >= stage.holdTimeHours) {
                    stageCompleted = true;
                    #if DEBUG_FERMENTATION
                    Serial.printf("[Fase] ✅ Tempo atingido: %.1fh >= %.1fh (%.2f dias)\n",
                                 elapsedH, stage.holdTimeHours, stage.durationDays);
                    #endif
                }
            }
            break;

        case STAGE_RAMP:
            if (fermentacaoState.stageStartEpoch > 0 && 
                elapsedH >= (float)stage.rampTimeHours) {
                stageCompleted = true;
            }
            break;

        case STAGE_GRAVITY:
            if (targetReached && mySpindel.gravity <= stage.targetGravity) {
                stageCompleted = true;
            }
            break;

        case STAGE_GRAVITY_TIME:
            if (targetReached) {
                bool timeoutReached = (fermentacaoState.stageStartEpoch > 0 && 
                                      elapsedH >= stage.maxTimeHours);
                if (mySpindel.gravity <= stage.targetGravity || timeoutReached) {
                    stageCompleted = true;
                }
            }
            break;
    }

    // =====================================================
    // TRANSIÇÃO PARA PRÓXIMA ETAPA
    // =====================================================
    if (stageCompleted) {
        
        int nextStageIndex = fermentacaoState.currentStageIndex + 1;
        
        if (nextStageIndex < fermentacaoState.totalStages) {
            // Notifica servidor
            if (httpClient.isConnected()) {
                httpClient.updateStageIndex(fermentacaoState.activeId, nextStageIndex);
            }
            
            // Atualiza estado local
            fermentacaoState.currentStageIndex = nextStageIndex;
            stageStarted = false;
            fermentacaoState.stageStartEpoch = 0;
            fermentacaoState.targetReachedSent = false;
            
            brewPiControl.reset();
            saveStateToEEPROM();

            LOG_FERMENTATION("[Fase] ↪️ Indo para etapa " + String(fermentacaoState.currentStageIndex + 1) + "/" + String(fermentacaoState.totalStages));
        } else {
            // ✅ CORREÇÃO: Última etapa concluída - NÃO incrementa o índice
            // Mantém o índice na última etapa válida para referência
            // O estado concluidaMantendoTemp controla o comportamento
            
            LOG_FERMENTATION(F("[Fase] 🎉 TODAS AS ETAPAS CONCLUÍDAS!"));
            LOG_FERMENTATION(F("[Fase] 🔒 Mantendo última temperatura até comando manual"));
            
            concluirFermentacaoMantendoTemperatura();
        }
    }
}

void verificarTargetAtingido() {
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    float currentTemp = getCurrentBeerTemp();
    float diff = abs(currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) {
        if (httpClient.notifyTargetReached(fermentacaoState.activeId)) {
            fermentacaoState.targetReachedSent = true;
            #if DEBUG_FERMENTATION
            Serial.println(F("[MySQL] 🎯 Temperatura alvo atingida!"));
            #endif
        } 
        #if DEBUG_FERMENTATION
        else {
            Serial.println(F("[MySQL] ❌ Falha ao notificar alvo"));
        }
        #endif
    }
}

// =====================================================
// ✅ WRAPPERS PARA FUNÇÕES DE ENVIO (mantém compatibilidade)
// =====================================================

void enviarEstadoCompleto() {
    enviarEstadoCompletoMySQL();
}

void enviarLeiturasSensores() {
    enviarLeiturasSensoresMySQL();
}