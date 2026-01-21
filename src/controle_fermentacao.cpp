// controle_fermentacao.cpp - Reescrito para integração BrewPi
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
    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    
    #if DEBUG_FERMENTATION
    if (!EEPROM.commit()) {
        Serial.println(F("[EEPROM] ❌ Falha ao salvar estado"));
    } else {
        Serial.print(F("[EEPROM] ✅ Estado salvo (início: "));
        Serial.print(formatTime(epoch));
        Serial.println(")");
    }
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
    Serial.println(formatTime(savedEpoch));
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
    #endif
}

// =====================================================
// CONTROLE DE TEMPERATURA - INTEGRAÇÃO BREWPI
// =====================================================

void updateTargetTemperature(float newTemp) {
    // Converte float para fixed-point BrewPi
    temperature temp = floatToTemp(newTemp);
    brewPiControl.setBeerTemp(temp);
    
    // Atualiza estado global (compatibilidade)
    fermentacaoState.tempTarget = newTemp;
    state.targetTemp = newTemp;
    
    #if DEBUG_FERMENTATION
    Serial.printf("[BrewPi] 🎯 Novo alvo: %.2f°C\n", newTemp);
    #endif
}

float getCurrentBeerTemp() {
    temperature temp = brewPiControl.getBeerTemp();
    float tempFloat = tempToFloat(temp);
    
    // Atualiza estado global (compatibilidade)
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
// Função para comprimir dados de estado
// =====================================================

void compressStateData(JsonDocument &doc) {
    #if DEBUG_ENVIODADOS
    Serial.println(F("[Compress] 🗜️ Iniciando compressão de dados..."));
    #endif
    
    // ========== 1. Comprimir mensagens ==========
    if (doc["message"].is<const char*>()) {
        const char* msg = doc["message"].as<const char*>();
        if (strstr(msg, "Fermentação concluída automaticamente")) {
            doc["msg"] = MSG_FCONC;
            doc.remove("message");
        }
    }
    
    // ========== 2. Comprimir status de controle ==========
    if (doc["control_status"].is<JsonObject>()) {
        JsonObject cs = doc["control_status"].as<JsonObject>();
        if (cs["state"].is<const char*>()) {
            const char* state = cs["state"].as<const char*>();
            
            if (strstr(state, "Cooling")) {
                cs["s"] = MSG_COOL;
                cs.remove("state");
            } else if (strstr(state, "Heating")) {
                cs["s"] = MSG_HEAT;
                cs.remove("state");
            } else if (strstr(state, "Waiting")) {
                cs["s"] = MSG_WAIT;
                cs.remove("state");
            } else if (strstr(state, "Idle")) {
                cs["s"] = MSG_IDLE;
                cs.remove("state");
            }
        }
    }
    
    // ========== 3. Comprimir tipo de etapa ==========
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        switch(stage.type) {
            case STAGE_TEMPERATURE: doc["st"] = ST_TEMP; break;
            case STAGE_RAMP: doc["st"] = ST_RAMP; break;
            case STAGE_GRAVITY: doc["st"] = ST_GRAV; break;
            case STAGE_GRAVITY_TIME: doc["st"] = ST_GRAVT; break;
        }
    }
    
    // ========== 4. Comprimir timeRemaining PRIMEIRO ==========
    // Isso cria doc["tr"] como array se timeRemaining existir
    bool trCreatedFromTimeRemaining = false;
    
    if (!doc["timeRemaining"].isNull() && doc["timeRemaining"].is<JsonObject>()) {
        JsonObject tr = doc["timeRemaining"].as<JsonObject>();
        const char* unit = tr["unit"] | "";
        const char* status = tr["status"] | "";
        
        // Formato detailed: [dias, horas, minutos, status]
        if (strcmp(unit, "detailed") == 0) {
            int days = tr["days"] | 0;
            int hours = tr["hours"] | 0;
            int minutes = tr["minutes"] | 0;
            
            JsonArray compactTR = doc["tr"].to<JsonArray>();
            compactTR.add(days);
            compactTR.add(hours);
            compactTR.add(minutes);
            
            if (strcmp(status, "running") == 0) {
                compactTR.add(MSG_RUN);
            } else if (strcmp(status, "waiting") == 0) {
                compactTR.add(MSG_WAIT);
            } else if (strcmp(status, "waiting_gravity") == 0) {
                compactTR.add(WG);
            } else {
                compactTR.add(status);
            }
            
            trCreatedFromTimeRemaining = true;
            doc.remove("timeRemaining");
            
            #if DEBUG_ENVIODADOS
            Serial.printf("[Compress] tr (detailed): [%d, %d, %d, %s]\n", 
                         days, hours, minutes, status);
            #endif
        } 
        // Formato antigo: [valor, unidade, status]
        else if (tr["value"].is<float>() || tr["value"].is<int>()) {
            float value = tr["value"].is<float>() ? tr["value"].as<float>() : (float)tr["value"].as<int>();
            
            JsonArray compactTR = doc["tr"].to<JsonArray>();
            compactTR.add(value);
            
            if (strcmp(unit, "hours") == 0) {
                compactTR.add(UNIT_H);
            } else if (strcmp(unit, "days") == 0) {
                compactTR.add(UNIT_D);
            } else if (strcmp(unit, "minutes") == 0) {
                compactTR.add(UNIT_M);
            } else if (strcmp(unit, "indefinite") == 0) {
                compactTR.add("ind");
            } else {
                compactTR.add(unit);
            }
            
            if (strcmp(status, "running") == 0) {
                compactTR.add(MSG_RUN);
            } else if (strcmp(status, "waiting") == 0) {
                compactTR.add(MSG_WAIT);
            } else if (strcmp(status, "waiting_gravity") == 0) {
                compactTR.add(WG);
            } else {
                compactTR.add(status);
            }
            
            trCreatedFromTimeRemaining = true;
            doc.remove("timeRemaining");
            
            #if DEBUG_ENVIODADOS
            Serial.printf("[Compress] tr (legacy): [%.1f, %s, %s]\n", 
                         value, unit, status);
            #endif
        }
    }
    
    // ========== 5. Mapeamento de campos ==========
    struct FieldMapping {
        const char* longName;
        const char* shortName;
    };
    
    FieldMapping fieldMappings[] = {
        {"config_name", "cn"},
        {"currentStageIndex", "csi"},
        {"totalStages", "ts"},
        {"stageTargetTemp", "stt"},
        {"pidTargetTemp", "ptt"},
        {"currentTargetTemp", "ctt"},
        {"cooling", "c"},
        {"heating", "h"},
        {"status", "s"},
        {"config_id", "cid"},
        {"completedAt", "ca"},
        {"timestamp", "tms"},
        {"uptime_ms", "um"},
        {"rampProgress", "rp"}
        // targetReached NÃO está aqui - tratado separadamente
    };
    
    for (const auto& mapping : fieldMappings) {
        if (!doc[mapping.longName].isNull()) {
            doc[mapping.shortName] = doc[mapping.longName];
            doc.remove(mapping.longName);
        }
    }
    
    // ========== 6. Tratar targetReached ==========
    // Se tr já foi criado do timeRemaining, apenas remove targetReached
    // Senão, converte targetReached para tr (fallback de segurança)
    if (!doc["targetReached"].isNull()) {
        if (trCreatedFromTimeRemaining) {
            // tr já existe como array, apenas remove o booleano
            doc.remove("targetReached");
            #if DEBUG_ENVIODADOS
            Serial.println(F("[Compress] targetReached removido (já temos tr array)"));
            #endif
        } else {
            // Fallback: converte para tr booleano
            doc["tr"] = doc["targetReached"].as<bool>();
            doc.remove("targetReached");
            #if DEBUG_ENVIODADOS
            Serial.printf("[Compress] tr (fallback bool): %s\n", 
                         doc["tr"].as<bool>() ? "true" : "false");
            #endif
        }
    }
    
    #if DEBUG_ENVIODADOS
    Serial.println(F("[Compress] ✅ Compressão concluída"));
    #endif
}

// =====================================================
// CONTROLE DE ESTADO
// =====================================================

void concluirFermentacaoMantendoTemperatura() {
    #if DEBUG_FERMENTATION
    Serial.println(F("[Fase] ✅ Fermentação concluída - mantendo temperatura atual"));
    #endif
    
    JsonDocument doc;
    doc["s"] = MSG_CHOLD;  // status comprimido
    time_t completionEpoch = getCurrentEpoch();
    if (completionEpoch > 0) {
        doc["ca"] = completionEpoch;  // completedAt comprimido
    }
    doc["msg"] = MSG_FCONC;  // Mensagem comprimida
    doc["cid"] = fermentacaoState.activeId;  // config_id comprimido
    
    // Não precisa chamar compressStateData aqui porque já estamos usando chaves comprimidas
    
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

    // Reset do controle BrewPi
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
    
    // Reset do controle BrewPi na inicialização
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
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ⚠️ WiFi desconectado"));
        #endif
        isFirstCheck = false;
        return;
    }

    #if DEBUG_FERMENTATION
    Serial.println(F("\n========================================"));
    Serial.println(F("[MySQL] 🔍 INICIANDO BUSCA DE FERMENTAÇÃO"));
    Serial.println(F("========================================"));
    #endif

    JsonDocument doc;
    
    bool requestOk = httpClient.getActiveFermentation(doc);
    
    #if DEBUG_FERMENTATION
    Serial.printf("[MySQL] getActiveFermentation() retornou: %s\n", 
                  requestOk ? "TRUE" : "FALSE");
    #endif
    
    if (!requestOk) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ❌ Falha na requisição HTTP"));
        #endif
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
    #if DEBUG_FERMENTATION
    const char* name = doc["name"] | "";
    const char* status = doc["status"] | "";
    #endif
    int currentStageIndex = doc["currentStageIndex"] | 0;
    
    #if DEBUG_FERMENTATION
    Serial.println(F("\n[MySQL] 🔍 VALORES EXTRAÍDOS:"));
    Serial.printf("  active: %s\n", active ? "TRUE" : "FALSE");
    Serial.printf("  id: '%s' (length: %d)\n", id, strlen(id));
    Serial.printf("  name: '%s'\n", name);
    Serial.printf("  status: '%s'\n", status);
    Serial.printf("  currentStageIndex: %d\n", currentStageIndex);
    
    Serial.println(F("\n[MySQL] 🔍 ESTADO ATUAL DO SISTEMA:"));
    Serial.printf("  fermentacaoState.active: %s\n", 
                  fermentacaoState.active ? "TRUE" : "FALSE");
    Serial.printf("  fermentacaoState.activeId: '%s'\n", 
                  fermentacaoState.activeId);
    Serial.printf("  lastActiveId: '%s'\n", lastActiveId);
    #endif

    if (!isValidString(id)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ⚠️ ID é inválido ou vazio!"));
        #endif
        id = "";
    } else {
        #if DEBUG_FERMENTATION
        Serial.printf("[MySQL] ✅ ID válido: '%s'\n", id);
        #endif
    }

    #if DEBUG_FERMENTATION
    Serial.println(F("\n[MySQL] 🔍 DECISÃO:"));
    #endif

    if (active && isValidString(id)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("  → Fermentação ATIVA detectada no servidor"));
        #endif
        
        if (strcmp(id, lastActiveId) != 0) {
            #if DEBUG_FERMENTATION
            Serial.println(F("  → ID DIFERENTE do último conhecido"));
            Serial.printf("     Anterior: '%s'\n", lastActiveId);
            Serial.printf("     Novo:     '%s'\n", id);
            Serial.println(F("  → INICIANDO NOVA FERMENTAÇÃO"));
            #endif

            // Reset completo do BrewPi
            brewPiControl.reset();
            #if DEBUG_FERMENTATION
            Serial.println(F("[BrewPi] ✅ Sistema resetado para nova fermentação"));
            #endif
            
            fermentacaoState.active = true;
            fermentacaoState.concluidaMantendoTemp = false;
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            fermentacaoState.currentStageIndex = currentStageIndex;
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            #if DEBUG_FERMENTATION
            Serial.printf("[MySQL] 🔧 Carregando configuração ID: %s\n", id);
            #endif

            loadConfigParameters(id);

            stageStarted = false;
            fermentacaoState.targetReachedSent = false;
            fermentacaoState.stageStartEpoch = 0;

            saveStateToEEPROM();
            
            #if DEBUG_FERMENTATION
            Serial.println(F("[MySQL] ✅ CONFIGURAÇÃO CONCLUÍDA"));
            Serial.printf("  activeId: '%s'\n", fermentacaoState.activeId);
            Serial.printf("  tempTarget: %.1f°C\n", fermentacaoState.tempTarget);
            Serial.printf("  totalStages: %d\n", fermentacaoState.totalStages);
            #endif
        } else {
            #if DEBUG_FERMENTATION
            Serial.println(F("  → MESMO ID do último conhecido"));
            Serial.println(F("  → Fermentação já configurada"));
            #endif
            
            if (currentStageIndex != fermentacaoState.currentStageIndex) {
                #if DEBUG_FERMENTATION
                Serial.printf("  → Etapa mudou EXTERNAMENTE: %d -> %d\n", 
                            fermentacaoState.currentStageIndex, currentStageIndex);
                #endif
                
                fermentacaoState.currentStageIndex = currentStageIndex;
                stageStarted = false;
                fermentacaoState.stageStartEpoch = 0;
                fermentacaoState.targetReachedSent = false;
                        
                brewPiControl.reset();
                saveStateToEEPROM();
            }
        }
    } else if (fermentacaoState.active && !active) {
        if (fermentacaoState.concluidaMantendoTemp) {
            #if DEBUG_FERMENTATION
            Serial.println(F("  → Concluída localmente, mantendo temperatura (servidor offline)"));
            #endif
        } else {
            #if DEBUG_FERmentacaoState
            Serial.println(F("  → Fermentação estava ativa LOCALMENTE"));
            Serial.println(F("  → Servidor indica NÃO ATIVA"));
            Serial.println(F("  → DESATIVANDO"));
            #endif
            deactivateCurrentFermentation();
        }
    } else if (!active && !fermentacaoState.active) {
        #if DEBUG_FERMENTATION
        Serial.println(F("  → Nenhuma fermentação ativa"));
        Serial.println(F("  → Sistema em STANDBY"));
        #endif
        
        if (state.targetTemp == DEFAULT_TEMPERATURE) {
            brewPiControl.reset();
            #if DEBUG_FERMENTATION
            Serial.println(F("[BrewPi] ✅ Sistema resetado em modo standby"));
            #endif
        }
    } else if (!active && fermentacaoState.active) {
        #if DEBUG_FERMENTATION
        Serial.println(F("  → Servidor offline mas temos estado local"));
        Serial.println(F("  → MANTENDO fermentação local"));
        #endif
    }

    #if DEBUG_FERMENTATION
    Serial.println(F("========================================"));
    Serial.println(F("[MySQL] FIM DA VERIFICAÇÃO"));
    Serial.println(F("========================================\n"));
    #endif
    
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

    #if DEBUG_FERMENTATION
    Serial.printf("[MySQL] 🔧 Buscando config: %s\n", configId);
    #endif
    
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
    
    for (JsonVariant stage : stages) {
        if (count >= MAX_STAGES) {
            #if DEBUG_FERMENTATION
            Serial.println(F("[MySQL] ⚠️  Máximo de etapas excedido"));
            #endif
            break;
        }

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

        s.targetTemp = stage["targetTemp"] | 20.0f;
        s.startTemp = stage["startTemp"] | 20.0f;
        s.rampTimeHours = stage["rampTime"] | 0;
        s.durationDays = stage["duration"] | 0.0f;
        s.targetGravity = stage["targetGravity"] | 0.0f;
        s.timeoutDays = stage["timeoutDays"] | 0.0f;
        
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
        Serial.printf("║ Alvo etapa:       %6.1f°C          ║\n", 
                     fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp);
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
    
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Fase] ⚠️  Índice inválido"));
        #endif
        
        if (fermentacaoState.totalStages > 0) {
            fermentacaoState.currentStageIndex = 0;
            #if DEBUG_FERMENTATION
            Serial.println(F("[Fase] 🔄 Recomeçando da etapa 0"));
            #endif
        } else {
            deactivateCurrentFermentation();
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
        
        static unsigned long lastDebug2 = 0;
        unsigned long now = millis();
        if (now - lastDebug2 > 60000 && !fermentacaoState.targetReachedSent) {
            lastDebug2 = now;
            Serial.printf("[Fase] Aguardando alvo: Temp=%.1f°C, Alvo=%.1f°C, Diff=%.1f°C, Atingiu=%s\n",
                         currentTemp, stageTargetTemp, diff, targetReached ? "SIM" : "NÃO");
        }
        
        if (targetReached && !fermentacaoState.targetReachedSent) {
            fermentacaoState.targetReachedSent = true;
            
            if (fermentacaoState.stageStartEpoch == 0) {
                fermentacaoState.stageStartEpoch = nowEpoch;
                saveStateToEEPROM();
                Serial.printf("[Fase] 🎯 Temperatura FINAL da etapa atingida: %.1f°C!\n", stageTargetTemp);
                Serial.printf("[Fase] ⏱️  Contagem iniciada em: %s\n", formatTime(nowEpoch).c_str());
            }
        }
    } 
    else if (stage.type == STAGE_RAMP) {
        targetReached = true;
        
        if (fermentacaoState.stageStartEpoch == 0) {
            fermentacaoState.stageStartEpoch = nowEpoch;
            saveStateToEEPROM();
            Serial.println(F("[Fase] ⏱️  Contagem de rampa iniciada"));
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
                // Usa holdTimeHours do struct (já calculado)
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
                // Usa maxTimeHours do struct (já calculado)
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
        #if DEBUG_FERMENTATION
        Serial.printf("[Fase] ✅ Etapa %d/%d concluída após %.1fh (%.2f dias)\n", 
                    fermentacaoState.currentStageIndex + 1,
                    fermentacaoState.totalStages,
                    elapsedH,
                    elapsedH / 24.0f);
        #endif
      
        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        fermentacaoState.stageStartEpoch = 0;
        fermentacaoState.targetReachedSent = false;

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            saveStateToEEPROM();

            #if DEBUG_FERMENTATION
            Serial.printf("[Fase] ↪️  Indo para etapa %d/%d\n", 
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages);
            #endif
        } else {
            #if DEBUG_FERMENTATION
            Serial.println(F("[Fase] 🎉 TODAS AS ETAPAS CONCLUÍDAS!"));
            Serial.println(F("[Fase] 🌡️  Mantendo temperatura atual até comando manual"));
            #endif
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
// ✅ ENVIAR ESTADO COMPLETO
// =====================================================
void enviarEstadoCompleto() {
    // ========== VERIFICAÇÕES INICIAIS ==========
    if (!fermentacaoState.active && !fermentacaoState.concluidaMantendoTemp) {
        #if DEBUG_ENVIODADOS
        Serial.println(F("[Envio] ❌ Não enviando: fermentação não ativa"));
        #endif
        return;
    }
    
    if (!isValidString(fermentacaoState.activeId)) {
        #if DEBUG_ENVIODADOS
        Serial.println(F("[Envio] ❌ ID inválido"));
        #endif
        return;
    }
    
    // ========== CONTROLE DE TEMPO ENTRE ENVIOS ==========
    static unsigned long lastStateSend = 0;
    unsigned long now = millis();
    
    if (now - lastStateSend < 30000) {
        #if DEBUG_ENVIODADOS
        Serial.printf("[Envio] ⏳ Aguardando próximo envio: %lu ms restantes\n", 
                     30000 - (now - lastStateSend));
        #endif
        return;
    }
    
    lastStateSend = now;
    
    // ========== PREPARAÇÃO DOS DADOS ==========
    JsonDocument doc;
    
    // 1. Dados essenciais
    doc["config_id"] = fermentacaoState.activeId;
    doc["config_name"] = fermentacaoState.configName;
    doc["currentStageIndex"] = fermentacaoState.currentStageIndex;
    doc["totalStages"] = fermentacaoState.totalStages;
    
    if (fermentacaoState.concluidaMantendoTemp) {
        doc["status"] = "completed_holding_temp";
        doc["message"] = "Fermentação concluída - mantendo temperatura";
    } else {
        doc["status"] = "running";
    }
    
    // 2. Temperaturas
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        doc["stageTargetTemp"] = stage.targetTemp;
    }
    
    doc["pidTargetTemp"] = fermentacaoState.tempTarget;
    doc["currentTargetTemp"] = fermentacaoState.tempTarget;
    doc["targetReached"] = fermentacaoState.targetReachedSent;
    
    // ========== 3. CÁLCULO DO TEMPO RESTANTE ==========
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
        
        // Caso 1: stageStartEpoch válido - calcular tempo restante real
        if (fermentacaoState.stageStartEpoch > 0) {
            time_t nowEpoch = getCurrentEpoch();
            
            if (nowEpoch > 0) {
                float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0f;
                
                if (stage.type == STAGE_TEMPERATURE) {
                    if (fermentacaoState.targetReachedSent) {
                        // Usa holdTimeHours do struct
                        float remainingH = stage.holdTimeHours - elapsedH;
                        if (remainingH < 0) remainingH = 0;
                        
                        #if DEBUG_ENVIODADOS
                        Serial.printf("[DEBUG] stage.durationDays: %.2f\n", stage.durationDays);
                        Serial.printf("[DEBUG] stage.holdTimeHours: %.1f\n", stage.holdTimeHours);
                        Serial.printf("[DEBUG] elapsedH: %.1f\n", elapsedH);
                        Serial.printf("[DEBUG] remainingH: %.1f\n", remainingH);
                        #endif
                        
                        formatTimeRemaining(timeRemaining, remainingH, "running");
                    } else {
                        // Aguardando temperatura alvo
                        if (stage.durationDays >= 1.0f) {
                            timeRemaining["value"] = stage.durationDays;
                            timeRemaining["unit"] = "days";
                        } else {
                            timeRemaining["value"] = stage.holdTimeHours;
                            timeRemaining["unit"] = "hours";
                        }
                        timeRemaining["status"] = "waiting";
                    }
                }
                else if (stage.type == STAGE_RAMP) {
                    float remainingH = (float)stage.rampTimeHours - elapsedH;
                    if (remainingH < 0) remainingH = 0;
                    
                    formatTimeRemaining(timeRemaining, remainingH, "running");
                    
                    // Progresso da rampa
                    float progress = constrain(elapsedH / (float)stage.rampTimeHours, 0.0f, 1.0f);
                    doc["rampProgress"] = progress * 100.0f;
                }
                else if (stage.type == STAGE_GRAVITY_TIME) {
                    if (fermentacaoState.targetReachedSent) {
                        // Usa maxTimeHours do struct
                        float remainingH = stage.maxTimeHours - elapsedH;
                        if (remainingH < 0) remainingH = 0;
                        
                        formatTimeRemaining(timeRemaining, remainingH, "running");
                    } else {
                        if (stage.timeoutDays >= 1.0f) {
                            timeRemaining["value"] = stage.timeoutDays;
                            timeRemaining["unit"] = "days";
                        } else {
                            timeRemaining["value"] = stage.maxTimeHours;
                            timeRemaining["unit"] = "hours";
                        }
                        timeRemaining["status"] = "waiting";
                    }
                }
                else if (stage.type == STAGE_GRAVITY) {
                    timeRemaining["value"] = 0;
                    timeRemaining["unit"] = "indefinite";
                    timeRemaining["status"] = "waiting_gravity";
                }
            } else {
                // NTP não sincronizado
                setInitialTimeRemaining(timeRemaining, stage, fermentacaoState.targetReachedSent);
            }
        } 
        // Caso 2: stageStartEpoch == 0
        else {
            #if DEBUG_ENVIODADOS
            Serial.println(F("[DEBUG] stageStartEpoch == 0, criando timeRemaining inicial"));
            #endif
            
            setInitialTimeRemaining(timeRemaining, stage, fermentacaoState.targetReachedSent);
        }
    }
    
    // 4. Status do BrewPi
    DetailedControlStatus detailedStatus = brewPiControl.getDetailedStatus();
    
    doc["cooling"] = detailedStatus.coolerActive;
    doc["heating"] = detailedStatus.heaterActive;
    
    JsonObject controlStatus = doc["control_status"].to<JsonObject>();
    controlStatus["state"] = detailedStatus.stateName;
    controlStatus["is_waiting"] = detailedStatus.isWaiting;
    
    if (detailedStatus.isWaiting) {
        controlStatus["wait_reason"] = detailedStatus.waitReason;
        
        if (detailedStatus.waitTimeRemaining > 0) {
            controlStatus["wait_seconds"] = detailedStatus.waitTimeRemaining;
            
            char waitDisplay[16];
            if (detailedStatus.waitTimeRemaining < 60) {
                snprintf(waitDisplay, sizeof(waitDisplay), "%us", 
                        (unsigned int)detailedStatus.waitTimeRemaining);
            } else if (detailedStatus.waitTimeRemaining < 3600) {
                snprintf(waitDisplay, sizeof(waitDisplay), "%um", 
                        (unsigned int)(detailedStatus.waitTimeRemaining / 60));
            } else {
                snprintf(waitDisplay, sizeof(waitDisplay), "%uh", 
                        (unsigned int)(detailedStatus.waitTimeRemaining / 3600));
            }
            controlStatus["wait_display"] = waitDisplay;
        }
    }
    
    if (detailedStatus.peakDetection) {
        controlStatus["peak_detection"] = true;
        controlStatus["estimated_peak"] = detailedStatus.estimatedPeak;
    }
    
    // 5. Timestamp e uptime
    time_t nowEpoch = getCurrentEpoch();
    if (nowEpoch > 0) {
        doc["timestamp"] = nowEpoch;
    }
    
    doc["uptime_ms"] = millis();
    
    // 6. stageType para debug
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        const char* stageTypeStr = "";
        switch(stage.type) {
            case STAGE_TEMPERATURE: stageTypeStr = "t"; break;
            case STAGE_RAMP: stageTypeStr = "r"; break;
            case STAGE_GRAVITY: stageTypeStr = "g"; break;
            case STAGE_GRAVITY_TIME: stageTypeStr = "gt"; break;
        }
        doc["stageType"] = stageTypeStr;
    }
    
    // ========== COMPRESSÃO DOS DADOS ==========
    compressStateData(doc);
    
    // ========== ENVIO ==========
    #if DEBUG_ENVIODADOS
    bool sendSuccess = httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    Serial.printf("[Envio] Resultado: %s\n", sendSuccess ? "✅ Sucesso" : "❌ Falha");
    
    Serial.println(F("\n[DEBUG] DADOS ENVIADOS:"));
    serializeJsonPretty(doc, Serial);
    Serial.println();
    Serial.printf("[DEBUG] Heap livre: %d bytes\n", ESP.getFreeHeap());
    #else
    httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    #endif
}

// =====================================================
// ✅ FUNÇÕES AUXILIARES PARA timeRemaining
// =====================================================

// Formata o tempo restante em dias/horas/minutos
void formatTimeRemaining(JsonObject& timeRemaining, float remainingH, const char* status) {
    if (remainingH >= 24.0) {
        // Para mais de 1 dia, calcular dias, horas, minutos
        int totalMinutes = roundf(remainingH * 60.0);
        int days = totalMinutes / (24 * 60);
        int hours = (totalMinutes % (24 * 60)) / 60;
        int minutes = totalMinutes % 60;
        
        timeRemaining["days"] = days;
        timeRemaining["hours"] = hours;
        timeRemaining["minutes"] = minutes;
        timeRemaining["total_hours"] = remainingH;
        timeRemaining["unit"] = "detailed";
        
        #if DEBUG_ENVIODADOS
        Serial.printf("[DEBUG] Tempo detalhado: %dd %dh %dm\n", days, hours, minutes);
        #endif
    } else if (remainingH >= 1.0) {
        // Para menos de 1 dia mas mais de 1 hora
        int totalMinutes = roundf(remainingH * 60.0);
        int hours = totalMinutes / 60;
        int minutes = totalMinutes % 60;
        
        if (hours > 0) {
            timeRemaining["hours"] = hours;
            timeRemaining["minutes"] = minutes;
            timeRemaining["total_hours"] = remainingH;
            timeRemaining["unit"] = "detailed";
        } else {
            timeRemaining["value"] = totalMinutes;
            timeRemaining["unit"] = "minutes";
        }
        
        #if DEBUG_ENVIODADOS
        Serial.printf("[DEBUG] Tempo detalhado: %dh %dm\n", hours, minutes);
        #endif
    } else {
        // Menos de 1 hora, mostrar minutos
        int minutes = roundf(remainingH * 60.0);
        timeRemaining["value"] = minutes;
        timeRemaining["unit"] = "minutes";
    }
    
    timeRemaining["status"] = status;
}

// Define valores iniciais de timeRemaining baseado no tipo de etapa
void setInitialTimeRemaining(JsonObject& timeRemaining, FermentationStage& stage, bool targetReached) {
    switch (stage.type) {
        case STAGE_TEMPERATURE:
            if (targetReached) {
                // Temperatura já atingida - usa holdTimeHours do struct
                if (stage.holdTimeHours >= 24.0f) {
                    int days = (int)(stage.holdTimeHours / 24.0f);
                    int hours = (int)(stage.holdTimeHours - (days * 24.0f));
                    
                    timeRemaining["days"] = days;
                    timeRemaining["hours"] = hours;
                    timeRemaining["minutes"] = 0;
                    timeRemaining["total_hours"] = stage.holdTimeHours;
                    timeRemaining["unit"] = "detailed";
                } else if (stage.holdTimeHours >= 1.0f) {
                    int hours = (int)stage.holdTimeHours;
                    int minutes = (int)((stage.holdTimeHours - hours) * 60.0f);
                    
                    timeRemaining["hours"] = hours;
                    timeRemaining["minutes"] = minutes;
                    timeRemaining["total_hours"] = stage.holdTimeHours;
                    timeRemaining["unit"] = "detailed";
                } else {
                    int minutes = (int)(stage.holdTimeHours * 60.0f);
                    if (minutes < 1) minutes = 1;
                    timeRemaining["value"] = minutes;
                    timeRemaining["unit"] = "minutes";
                }
                timeRemaining["status"] = "running";
            } else {
                // Aguardando temperatura
                if (stage.durationDays >= 1.0f) {
                    timeRemaining["value"] = stage.durationDays;
                    timeRemaining["unit"] = "days";
                } else {
                    timeRemaining["value"] = stage.holdTimeHours;
                    timeRemaining["unit"] = "hours";
                }
                timeRemaining["status"] = "waiting";
            }
            break;
            
        case STAGE_RAMP:
            if (targetReached) {
                int hours = stage.rampTimeHours;
                
                if (hours >= 24) {
                    int days = hours / 24;
                    int remainingHours = hours % 24;
                    
                    timeRemaining["days"] = days;
                    timeRemaining["hours"] = remainingHours;
                    timeRemaining["minutes"] = 0;
                    timeRemaining["total_hours"] = (float)hours;
                    timeRemaining["unit"] = "detailed";
                } else {
                    timeRemaining["hours"] = hours;
                    timeRemaining["minutes"] = 0;
                    timeRemaining["total_hours"] = (float)hours;
                    timeRemaining["unit"] = "detailed";
                }
                timeRemaining["status"] = "running";
            } else {
                timeRemaining["value"] = stage.rampTimeHours;
                timeRemaining["unit"] = "hours";
                timeRemaining["status"] = "waiting";
            }
            break;
            
        case STAGE_GRAVITY:
            timeRemaining["value"] = 0;
            timeRemaining["unit"] = "indefinite";
            timeRemaining["status"] = "waiting_gravity";
            break;
            
        case STAGE_GRAVITY_TIME:
            if (targetReached) {
                // Usa maxTimeHours do struct
                if (stage.maxTimeHours >= 24.0f) {
                    int days = (int)(stage.maxTimeHours / 24.0f);
                    int hours = (int)(stage.maxTimeHours - (days * 24.0f));
                    
                    timeRemaining["days"] = days;
                    timeRemaining["hours"] = hours;
                    timeRemaining["minutes"] = 0;
                    timeRemaining["total_hours"] = stage.maxTimeHours;
                    timeRemaining["unit"] = "detailed";
                } else if (stage.maxTimeHours >= 1.0f) {
                    int hours = (int)stage.maxTimeHours;
                    int minutes = (int)((stage.maxTimeHours - hours) * 60.0f);
                    
                    timeRemaining["hours"] = hours;
                    timeRemaining["minutes"] = minutes;
                    timeRemaining["total_hours"] = stage.maxTimeHours;
                    timeRemaining["unit"] = "detailed";
                } else {
                    int minutes = (int)(stage.maxTimeHours * 60.0f);
                    if (minutes < 1) minutes = 1;
                    timeRemaining["value"] = minutes;
                    timeRemaining["unit"] = "minutes";
                }
                timeRemaining["status"] = "running";
            } else {
                if (stage.timeoutDays >= 1.0f) {
                    timeRemaining["value"] = stage.timeoutDays;
                    timeRemaining["unit"] = "days";
                } else {
                    timeRemaining["value"] = stage.maxTimeHours;
                    timeRemaining["unit"] = "hours";
                }
                timeRemaining["status"] = "waiting";
            }
            break;
    }
    
    #if DEBUG_ENVIODADOS
    Serial.printf("[DEBUG] setInitialTimeRemaining: targetReached=%s, status=%s\n",
                 targetReached ? "true" : "false",
                 timeRemaining["status"].as<const char*>());
    #endif
}

// =====================================================
// ENVIAR LEITURAS DOS SENSORES
// =====================================================
void enviarLeiturasSensores() {
    if (!fermentacaoState.active && !fermentacaoState.concluidaMantendoTemp) {
        return;
    }

    static unsigned long lastSensorReading = 0;
    unsigned long now = millis();
    if (now - lastSensorReading < READINGS_UPDATE_INTERVAL) {
        return;
    }
    lastSensorReading = now;

    float tempFermenter, tempFridge;
    if (!readConfiguredTemperatures(tempFermenter, tempFridge)) {
        return;
    }

    float gravity = mySpindel.gravity;

    httpClient.sendReading(fermentacaoState.activeId, tempFridge, 
                           tempFermenter, fermentacaoState.tempTarget, gravity);
}