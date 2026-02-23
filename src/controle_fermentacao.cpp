// controle_fermentacao.cpp - Reescrito para integração BrewPi
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>
#include <time.h>

#include "definitions.h"
#include "estruturas.h"
#include "globais.h"
#include "http_client.h"
#include "BrewPiStructs.h"
#include "BrewPiTempControl.h"
#include "controle_fermentacao.h"
#include "fermentacao_stages.h"
#include "gerenciador_sensores.h"
#include "debug_config.h"
#include "message_codes.h"
#include "mysql_sender.h"

extern FermentadorHTTPClient httpClient;

// Instância Preferences para fermentação
Preferences prefsFerment;

// =====================================================
// VARIÁVEIS DE CONTROLE
// =====================================================
unsigned long lastActiveCheck = 0;
char lastActiveId[64] = "";
bool isFirstCheck = true;
bool stageStarted = false;
bool justBootedWithState = false;

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
            prefsFerment.begin("ferment", true);  // read-only
            
            // ✅ CORRIGIDO: Usa ULong64
            lastValidEpoch = (time_t)prefsFerment.getULong64("lastEpoch", 0);
            lastValidMillis = prefsFerment.getULong("lastMillis", 0);
            
            prefsFerment.end();
            epochInitialized = true;

            #if DEBUG_FERMENTATION
            if (lastValidEpoch > 1577836800L) {
                Serial.print(F("[NTP] ⚠️  Usando backup Preferences: "));
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
        
        prefsFerment.begin("ferment", false);  // read-write
        
        // ✅ CORRIGIDO: Usa ULong64
        prefsFerment.putULong64("lastEpoch", (uint64_t)lastValidEpoch);
        prefsFerment.putULong("lastMillis", lastValidMillis);
        
        prefsFerment.end();
        
        lastBackup = millis();
    }
    
    return now;
}

// =====================================================
// PREFERENCES (substitui EEPROM)
// =====================================================

void saveStateToPreferences() {
    prefsFerment.begin("ferment", false);  // read-write

    prefsFerment.putString("activeId", fermentacaoState.activeId);
    prefsFerment.putInt("stageIdx", fermentacaoState.currentStageIndex);
    prefsFerment.putULong64("stageStart", (uint64_t)fermentacaoState.stageStartEpoch);
    
    prefsFerment.putBool("stageStrtd", stageStarted);
    prefsFerment.putBool("tgtReached", fermentacaoState.targetReachedSent);
    prefsFerment.putBool("cfgSaved", true);
    
    prefsFerment.end();
    
    #if DEBUG_FERMENTATION
    Serial.print(F("[Prefs] ✅ Estado salvo (início: "));
    Serial.print(formatTime(fermentacaoState.stageStartEpoch));
    Serial.printf(", targetReached: %s)\n", 
                 fermentacaoState.targetReachedSent ? "true" : "false");
    #endif
}

void loadStateFromPreferences() {
#ifdef DEBUG_EEPROM
    Serial.println(F("\n╔════════════════════════════════════════════╗"));
    Serial.println(F("║   loadStateFromPreferences() CHAMADO      ║"));
    Serial.println(F("╚════════════════════════════════════════════╝"));
    Serial.println(F("Abrindo namespace 'ferment'..."));
#endif

    if (!prefsFerment.begin("ferment", true)) {
#ifdef DEBUG_EEPROM
        Serial.println(F("❌ ERRO: prefsFerment.begin() falhou!"));
#endif
        return;
    }

#ifdef DEBUG_EEPROM
    Serial.println(F("✅ Namespace aberto com sucesso"));
#endif

bool cfgSaved = prefsFerment.getBool("cfgSaved", false);

#ifdef DEBUG_EEPROM
    Serial.print(F("cfgSaved lido: "));
    Serial.println(cfgSaved ? "true" : "false");
#endif

if (!cfgSaved) {
        prefsFerment.end();
#ifdef DEBUG_EEPROM
        Serial.println(F("❌ cfgSaved = 0, nenhum estado salvo"));
        Serial.println(F("╚════════════════════════════════════════════╝\n"));
#endif
        return;
    }

#ifdef DEBUG_EEPROM
    Serial.println(F("✅ cfgSaved OK, carregando dados..."));
#endif

    String activeIdStr = prefsFerment.getString("activeId", "");
    
#ifdef DEBUG_EEPROM
    Serial.print(F("activeId lido: '"));
    Serial.print(activeIdStr);
    Serial.println(F("'"));
#endif

    safe_strcpy(fermentacaoState.activeId, activeIdStr.c_str(),
                sizeof(fermentacaoState.activeId));

    if (!isValidString(fermentacaoState.activeId)) {
        prefsFerment.end();
#ifdef DEBUG_EEPROM
        Serial.println(F("❌ activeId inválido, limpando..."));
#endif
        clearPreferences();
        fermentacaoState.clear();
#ifdef DEBUG_EEPROM
        Serial.println(F("╚════════════════════════════════════════════╝\n"));
#endif
        return;
    }

#ifdef DEBUG_EEPROM
    Serial.println(F("✅ activeId válido"));
#endif

    fermentacaoState.currentStageIndex = prefsFerment.getInt("stageIdx", 0);
#ifdef DEBUG_EEPROM
    Serial.print(F("stageIdx lido: "));
    Serial.println(fermentacaoState.currentStageIndex);
#endif

    fermentacaoState.stageStartEpoch = (time_t)prefsFerment.getULong64("stageStart", 0);
#ifdef DEBUG_EEPROM
    Serial.print(F("stageStart lido: "));
    Serial.println((unsigned long)fermentacaoState.stageStartEpoch);
#endif

stageStarted = prefsFerment.getBool("stageStrtd", false);
#ifdef DEBUG_EEPROM
    Serial.print(F("stageStrtd lido: "));
    Serial.println(stageStarted ? F("true") : F("false"));
#endif

fermentacaoState.targetReachedSent = prefsFerment.getBool("tgtReached", false);
#ifdef DEBUG_EEPROM
    Serial.print(F("tgtReached lido: "));
    Serial.println(fermentacaoState.targetReachedSent ? F("true") : F("false"));
#endif

    prefsFerment.end();
#ifdef DEBUG_EEPROM
    Serial.println(F("✅ Namespace fechado"));
#endif

    fermentacaoState.active = isValidString(fermentacaoState.activeId);
#ifdef DEBUG_EEPROM
    Serial.print(F("fermentacaoState.active definido: "));
    Serial.println(fermentacaoState.active ? F("true") : F("false"));
#endif

    if (fermentacaoState.active && !isValidString(fermentacaoState.activeId)) {
#ifdef DEBUG_EEPROM
        Serial.println(F("❌ Estado inconsistente detectado!"));
#endif
        clearPreferences();
        fermentacaoState.clear();
        fermentacaoState.tempTarget = 20.0;
        state.targetTemp = 20.0;
#ifdef DEBUG_EEPROM
        Serial.println(F("╚════════════════════════════════════════════╝\n"));
#endif
        return;
    }

if (isValidString(fermentacaoState.activeId)) {
    safe_strcpy(lastActiveId, fermentacaoState.activeId, sizeof(lastActiveId));
    
    // ✅ Ativa proteção se restaurou estado válido
    if (fermentacaoState.active && fermentacaoState.stageStartEpoch > 0) {
        justBootedWithState = true;
        
        #ifdef DEBUG_EEPROM
        Serial.println(F("🔄 MODO RESTAURAÇÃO ATIVADO"));
        Serial.print(F("   stageStartEpoch: "));
        Serial.println((unsigned long)fermentacaoState.stageStartEpoch);
        Serial.println(F("   Proteção ativa por 60 segundos"));
        #endif
    }

    #ifdef DEBUG_EEPROM
    Serial.print(F("✅ lastActiveId restaurado: '"));
    Serial.print(lastActiveId);
    Serial.println(F("'"));
    #endif
}

#ifdef DEBUG_EEPROM
    Serial.println(F("\n╔════════════════════════════════════════════╗"));
    Serial.println(F("║   ✅ ESTADO RESTAURADO COM SUCESSO        ║"));
    Serial.println(F("╠════════════════════════════════════════════╣"));

    Serial.print(F("║ ID:              "));
    Serial.print(fermentacaoState.activeId);
    for (int i = strlen(fermentacaoState.activeId); i < 24; i++) Serial.print(' ');
    Serial.println(F(" ║"));

    Serial.print(F("║ Etapa:           "));
    Serial.print(fermentacaoState.currentStageIndex + 1);
    for (int i = (fermentacaoState.currentStageIndex + 1 < 10 ? 23 : 22); i < 24; i++) Serial.print(' ');
    Serial.println(F(" ║"));

    Serial.print(F("║ stageStartEpoch: "));
    unsigned long epoch = (unsigned long)fermentacaoState.stageStartEpoch;
    Serial.print(epoch);
    int digits = epoch == 0 ? 1 : (int)log10(epoch) + 1;
    for (int i = digits; i < 24; i++) Serial.print(' ');
    Serial.println(F(" ║"));

    Serial.print(F("║ targetReached:   "));
    Serial.print(fermentacaoState.targetReachedSent ? F("true") : F("false"));
    for (int i = (fermentacaoState.targetReachedSent ? 4 : 5); i < 24; i++) Serial.print(' ');
    Serial.println(F(" ║"));

    Serial.print(F("║ stageStarted:    "));
    Serial.print(stageStarted ? F("true") : F("false"));
    for (int i = (stageStarted ? 4 : 5); i < 24; i++) Serial.print(' ');
    Serial.println(F(" ║"));

    if (fermentacaoState.stageStartEpoch > 0) {
        time_t now = time(nullptr);
        if (now > 1577836800L) {
            float elapsed = difftime(now, fermentacaoState.stageStartEpoch) / 3600.0f;

            Serial.println(F("╠════════════════════════════════════════════╣"));
            
            Serial.print(F("║ ⏱️  Tempo decorrido: "));
            Serial.print(elapsed, 1);
            Serial.print(F(" horas"));
            for (int i = 11; i < 18; i++) Serial.print(' ');
            Serial.println(F(" ║"));
            
            Serial.print(F("║                    ("));
            Serial.print(elapsed / 24.0f, 2);
            Serial.print(F(" dias)"));
            for (int i = 9; i < 17; i++) Serial.print(' ');
            Serial.println(F(" ║"));
        }
    }

    Serial.println(F("╚════════════════════════════════════════════╝\n"));
#endif
}

void clearPreferences() {
    prefsFerment.begin("ferment", false);
    prefsFerment.clear();  // Limpa todas as chaves do namespace
    prefsFerment.end();
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[Prefs] Namespace ferment limpo"));
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
    
    if (httpClient.isConnected()) {
        httpClient.updateStageIndex(fermentacaoState.activeId, fermentacaoState.totalStages);
    }
    
    JsonDocument doc;
    doc["s"] = MSG_CHOLD;
    time_t completionEpoch = getCurrentEpoch();
    if (completionEpoch > 0) {
        doc["ca"] = completionEpoch;
    }
    doc["msg"] = MSG_FCONC;
    doc["cid"] = fermentacaoState.activeId;
    
    JsonArray trArray = doc["tr"].to<JsonArray>();
    trArray.add(MSG_TC);
    
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
    clearPreferences();
    saveStateToPreferences();
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[BrewPi] ✅ Sistema resetado na desativação"));
    #endif
}

void setupActiveListener() {
    #if DEBUG_FERMENTATION
    Serial.println(F("[MySQL] Sistema inicializado"));
    #endif

    loadStateFromPreferences();
    
    if (fermentacaoState.active) {
        if (fermentacaoState.targetReachedSent && fermentacaoState.stageStartEpoch == 0) {
            #if DEBUG_FERMENTATION
            Serial.println(F("[Prefs] ⚠️ Estado inconsistente detectado!"));
            Serial.println(F("[Prefs] targetReachedSent=true mas stageStartEpoch=0"));
            Serial.println(F("[Prefs] Resetando targetReachedSent para false"));
            #endif
            
            fermentacaoState.targetReachedSent = false;
            saveStateToPreferences();
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

// =====================================================
// VERIFICAÇÃO DE COMANDOS DO SITE E FERMENTAÇÃO ATIVA
// =====================================================

void getTargetFermentacao() {
    unsigned long now = millis();

    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_FERMENTATION(F("[MySQL] WiFi desconectado"));
        isFirstCheck = false;
        return;
    }

    LOG_FERMENTATION(F("\n========================================"));
    LOG_FERMENTATION(F("[MySQL] INICIANDO BUSCA DE FERMENTAÇÃO"));
    LOG_FERMENTATION(F("========================================"));

    JsonDocument doc;
    
    bool requestOk = httpClient.getActiveFermentation(doc);
    
    LOG_FERMENTATION("[MySQL] getActiveFermentation() retornou: " + String(requestOk ? "TRUE" : "FALSE"));
    
    if (!requestOk) {
        LOG_FERMENTATION(F("[MySQL] Falha na requisição HTTP"));
        isFirstCheck = false;
        return;
    }

    #if DEBUG_FERMENTATION
    Serial.println(F("\n[MySQL] DOCUMENTO JSON RECEBIDO:"));
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
    
    LOG_FERMENTATION(F("\n[MySQL] VALORES EXTRAÍDOS:"));
    LOG_FERMENTATION("  active: " + String(active ? "TRUE" : "FALSE"));
    LOG_FERMENTATION("  id: '" + String(id) + "' (length: " + String(strlen(id)) + ")");
    LOG_FERMENTATION("  serverStageIndex: " + String(serverStageIndex));
    
    LOG_FERMENTATION(F("\n[MySQL] ESTADO ATUAL DO SISTEMA:"));
    LOG_FERMENTATION("  fermentacaoState.active: " + String(fermentacaoState.active ? "TRUE" : "FALSE"));
    LOG_FERMENTATION("  fermentacaoState.activeId: '" + String(fermentacaoState.activeId) + "'");
    LOG_FERMENTATION("  fermentacaoState.currentStageIndex: " + String(fermentacaoState.currentStageIndex));
    LOG_FERMENTATION("  lastActiveId: '" + String(lastActiveId) + "'");

    if (!isValidString(id)) {
        LOG_FERMENTATION(F("[MySQL] ID é inválido ou vazio!"));
        id = "";
    } else {
        LOG_FERMENTATION("[MySQL] ID válido: '" + String(id) + "'");
    }

    LOG_FERMENTATION(F("\n[MySQL] DECISÃO:"));

    if (active && isValidString(id)) {
        LOG_FERMENTATION(F("  → Fermentação ATIVA detectada no servidor"));
        
        if (strcmp(id, lastActiveId) != 0) {
            // ✅ PROTEÇÃO DE BOOT: Se acabamos de restaurar do Preferences, não aceita mudança por 60s
            if (justBootedWithState) {
                LOG_FERMENTATION(F("\n⚠️ [PROTEÇÃO BOOT] ID diferente, mas acabamos de restaurar!"));
                LOG_FERMENTATION("   Preferences: ID='" + String(fermentacaoState.activeId) + 
                                "', stageStartEpoch=" + String((unsigned long)fermentacaoState.stageStartEpoch));
                LOG_FERMENTATION("   Servidor:    ID='" + String(id) + "'");
                
                static unsigned long bootProtectionStart = millis();
                unsigned long protectionDuration = 60000;  // 60 segundos
                
                if (millis() - bootProtectionStart < protectionDuration) {
                    unsigned long remaining = (protectionDuration - (millis() - bootProtectionStart)) / 1000;
                    LOG_FERMENTATION("   ⏸️  Ignorando por mais " + String(remaining) + " segundos");
                    return;  // Mantém estado local
                }
                
                LOG_FERMENTATION(F("   ✅ Período de proteção expirado, aceitando mudança"));
                justBootedWithState = false;  // Desativa proteção
            }
                        // =====================================================
            // NOVA FERMENTAÇÃO DETECTADA
            // =====================================================
            LOG_FERMENTATION(F("  → ID DIFERENTE do último conhecido"));
            LOG_FERMENTATION("     Anterior: '" + String(lastActiveId) + "'");
            LOG_FERMENTATION("     Novo:     '" + String(id) + "'");
            LOG_FERMENTATION(F("  → INICIANDO NOVA FERMENTAÇÃO"));

            brewPiControl.reset();
            LOG_FERMENTATION(F("[BrewPi] Sistema resetado para nova fermentação"));
            
            fermentacaoState.active = true;
            fermentacaoState.concluidaMantendoTemp = false;
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            fermentacaoState.currentStageIndex = serverStageIndex;
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            LOG_FERMENTATION("[MySQL] Carregando configuração ID: " + String(id));

            loadConfigParameters(id);

            // ✅ NOVA FERMENTAÇÃO: Reseta tudo
            stageStarted = false;
            fermentacaoState.targetReachedSent = false;
            fermentacaoState.stageStartEpoch = 0;

            saveStateToPreferences();

        // ✅ Envia heartbeat imediato com temperaturas atuais
        if (httpClient.isConnected()) {
            temperature beerTemp = brewPiControl.getBeerTemp();
            temperature fridgeTemp = brewPiControl.getFridgeTemp();
            httpClient.sendHeartbeat(
                atoi(fermentacaoState.activeId),
                brewPiControl.getDetailedStatus(),
                beerTemp,
                fridgeTemp
            );
        }
            
            LOG_FERMENTATION(F("[MySQL] CONFIGURAÇÃO CONCLUÍDA"));
            LOG_FERMENTATION("  activeId: '" + String(fermentacaoState.activeId) + "'");
            LOG_FERMENTATION("  tempTarget: " + String(fermentacaoState.tempTarget, 1) + "°C");
            LOG_FERMENTATION("  totalStages: " + String(fermentacaoState.totalStages));
            
        } else {
            // =====================================================
            // MESMA FERMENTAÇÃO - VERIFICAR SINCRONIZAÇÃO DE ETAPA
            // =====================================================
            LOG_FERMENTATION(F("  → MESMO ID do último conhecido"));
            LOG_FERMENTATION(F("  → Fermentação já configurada"));
            
            // ✅ CRÍTICO: Verifica se totalStages foi perdido (após reboot)
            if (fermentacaoState.totalStages == 0) {
                LOG_FERMENTATION(F("  ⚠️ totalStages = 0 após reboot"));
                LOG_FERMENTATION(F("  → Recarregando configuração do servidor"));
                loadConfigParameters(id);
                
                if (fermentacaoState.totalStages > 0) {
                    LOG_FERMENTATION("  ✅ Configuração recarregada: " + 
                                   String(fermentacaoState.totalStages) + " etapas");
                } else {
                    LOG_FERMENTATION(F("  ❌ Falha ao recarregar configuração!"));
                }
            }

            // ✅ SINCRONIZAÇÃO: Verifica timestamp do servidor
            unsigned long serverStageStartEpoch = doc["stageStartEpoch"] | 0;
            bool serverTargetReached = doc["targetReached"] | false;
            
            if (serverStageStartEpoch > 0 && serverStageStartEpoch != fermentacaoState.stageStartEpoch) {
                LOG_FERMENTATION(F("  → Timestamp diferente - sincronizando com servidor"));
                LOG_FERMENTATION("     Local:    " + String((unsigned long)fermentacaoState.stageStartEpoch));
                LOG_FERMENTATION("     Servidor: " + String(serverStageStartEpoch));
                
                fermentacaoState.stageStartEpoch = (time_t)serverStageStartEpoch;
                fermentacaoState.targetReachedSent = serverTargetReached;
                stageStarted = true;
                
                saveStateToPreferences();
                LOG_FERMENTATION(F("  → Timestamp sincronizado!"));
                
                // ✅ Desativa proteção após sincronização bem-sucedida
                if (justBootedWithState) {
                    justBootedWithState = false;
                    LOG_FERMENTATION(F("  ✅ Proteção de boot desativada - sincronização completa"));
                }
            }
            
            if (serverStageIndex != fermentacaoState.currentStageIndex) {
                LOG_FERMENTATION(F("  → Diferença de etapa detectada!"));
                LOG_FERMENTATION("     Local:    " + String(fermentacaoState.currentStageIndex));
                LOG_FERMENTATION("     Servidor: " + String(serverStageIndex));
                
                // ✅ CORREÇÃO: Só aceita índice do servidor se for MAIOR que o local
                if (serverStageIndex > fermentacaoState.currentStageIndex) {
                    LOG_FERMENTATION(F("  → Servidor à frente - aceitando mudança externa"));
                    
                    // Atualiza para o índice do servidor
                    fermentacaoState.currentStageIndex = serverStageIndex;
                    
                    // ✅ Reset para NOVA etapa (mudança forçada pelo servidor)
                    stageStarted = false;
                    fermentacaoState.stageStartEpoch = 0;
                    fermentacaoState.targetReachedSent = false;
                            
                    brewPiControl.reset();
                    saveStateToPreferences();
                    
                    LOG_FERMENTATION("  → Etapa atualizada para " + String(serverStageIndex));
                    
                } else if (serverStageIndex < fermentacaoState.currentStageIndex) {
                    // ✅ Local está à frente do servidor - servidor desatualizado
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
                // Se serverStageIndex == fermentacaoState.currentStageIndex, não faz nada
                
            } else {
                // ✅ Etapas sincronizadas - tudo OK
                LOG_FERMENTATION(F("  → Etapas sincronizadas (local == servidor)"));
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
            LOG_FERMENTATION(F("[BrewPi] Sistema resetado em modo standby"));
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

    LOG_FERMENTATION("[MySQL] Buscando config: " + String(configId));
    
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

        s.targetTemp = jsonToFloat(stage["targetTemp"], 20.0f);
        s.startTemp = jsonToFloat(stage["startTemp"], 20.0f);
        s.rampTimeHours = (int)jsonToFloat(stage["rampTime"], 0.0f);
        s.durationDays = jsonToFloat(stage["duration"], 0.0f);
        s.targetGravity = jsonToFloat(stage["targetGravity"], 0.0f);
        s.timeoutDays = jsonToFloat(stage["timeoutDays"], 0.0f);
        
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
    
    if (fermentacaoState.concluidaMantendoTemp) {
        #if DEBUG_FERMENTATION
        static unsigned long lastHoldDebug = 0;
        if (millis() - lastHoldDebug > 300000) {
            lastHoldDebug = millis();
            Serial.printf("[Fase] 🔒 Mantendo temperatura %.1f°C (aguardando comando manual)\n",
                         fermentacaoState.tempTarget);
        }
        #endif
        return;
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
    
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[Fase] ℹ️  Todas as etapas concluídas"));
        #endif
        
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
    // ✅ INÍCIO DE ETAPA - CORRIGIDO COM DETECÇÃO DE RESTAURAÇÃO
    // =====================================================
    if (!stageStarted) {
        // ✅ VERIFICA SE É RESTAURAÇÃO (já tinha começado antes do reinício)
        bool isRestoration = (fermentacaoState.stageStartEpoch > 0 && 
                             fermentacaoState.targetReachedSent);
        
        #if DEBUG_FERMENTATION
            Serial.println(F("\n═══════════════════════════════════════════════"));
            Serial.print(F("DEBUG RESTAURAÇÃO:\n"));
            Serial.print(F("  stageStartEpoch = "));
            Serial.println((unsigned long)fermentacaoState.stageStartEpoch);
            Serial.print(F("  targetReachedSent = "));
            Serial.println(fermentacaoState.targetReachedSent ? "TRUE" : "FALSE");
            Serial.print(F("  isRestoration = "));
            Serial.println(isRestoration ? "TRUE" : "FALSE");
            Serial.println(F("═══════════════════════════════════════════════\n"));
        #endif
        
        stageStarted = true;
        
        if (isRestoration) {
            // =====================================================
            // ✅ RESTAURAÇÃO: Mantém tudo que foi salvo
            // =====================================================
            #if DEBUG_FERMENTATION
            Serial.println(F("\n╔════════════════════════════════════════════╗"));
            Serial.println(F("║     🔄 RESTAURAÇÃO APÓS REINÍCIO          ║"));
            Serial.println(F("╠════════════════════════════════════════════╣"));
            Serial.printf("║ Etapa:        %d/%d                        ║\n",
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages);
            Serial.print(F("║ Início:       "));
            Serial.print(formatTime(fermentacaoState.stageStartEpoch).substring(0, 19));
            Serial.println(F(" ║"));
            
            time_t elapsed = nowEpoch - fermentacaoState.stageStartEpoch;
            Serial.printf("║ Decorrido:    %.1f horas (%.2f dias)      ║\n", 
                         elapsed / 3600.0f, elapsed / 86400.0f);
            
            if (stage.type == STAGE_TEMPERATURE) {
                float remaining = stage.holdTimeHours - (elapsed / 3600.0f);
                if (remaining < 0) remaining = 0;
                Serial.printf("║ Restante:     %.1f horas (%.2f dias)      ║\n", 
                             remaining, remaining / 24.0f);
            }
            Serial.println(F("╚════════════════════════════════════════════╝\n"));
            #endif
            
            // ✅ NÃO reseta targetReachedSent nem stageStartEpoch!
            // ✅ NÃO reseta PID (mantém estado de controle)
            
        } else {
            // =====================================================
            // ✅ NOVA ETAPA: Reseta tudo
            // =====================================================
            fermentacaoState.targetReachedSent = false;
            fermentacaoState.stageStartEpoch = 0;
            
            brewPiControl.reset();

            #if DEBUG_FERMENTATION
            Serial.println(F("[BrewPi] ✅ Sistema resetado para nova etapa"));
            #endif
        }
        
        // Define temperatura alvo
        float newTargetTemp;
        if (stage.type == STAGE_RAMP) {
            newTargetTemp = stage.startTemp;
        } else {
            newTargetTemp = stage.targetTemp;
        }
        
        updateTargetTemperature(newTargetTemp);
        saveStateToPreferences();
        
        #if DEBUG_FERMENTATION
        if (!isRestoration) {
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
        
        // ✅ CORRIGIDO: Salva atomicamente (define tudo primeiro, depois salva)
        if (targetReached) {
            if (!fermentacaoState.targetReachedSent) {
                // Captura timestamp ANTES de modificar variáveis
                time_t timestampToSave = nowEpoch;
                
                // Define todas as variáveis
                fermentacaoState.targetReachedSent = true;
                
                if (fermentacaoState.stageStartEpoch == 0) {
                    fermentacaoState.stageStartEpoch = timestampToSave;
                }
                
                // ✅ SALVA TUDO de uma vez (operação atômica)
                saveStateToPreferences();
                
                #if DEBUG_FERMENTATION
                Serial.printf("[Fase] 🎯 Temperatura FINAL da etapa atingida: %.1f°C!\n", stageTargetTemp);
                Serial.printf("[Fase] ⏱️  Contagem iniciada em: %s\n", formatTime(fermentacaoState.stageStartEpoch).c_str());
                #endif
            }
            // Recuperação de segurança
            else if (fermentacaoState.stageStartEpoch == 0) {
                #if DEBUG_FERMENTATION
                Serial.println(F("[Fase] ⚠️ RECUPERAÇÃO: targetReachedSent=true mas stageStartEpoch=0"));
                Serial.println(F("[Fase] ⚠️ Definindo stageStartEpoch com timestamp atual"));
                #endif
                
                fermentacaoState.stageStartEpoch = nowEpoch;
                saveStateToPreferences();
            }
        }
    } 
    else if (stage.type == STAGE_RAMP) {
        targetReached = true;
        
        if (fermentacaoState.stageStartEpoch == 0) {
            fermentacaoState.stageStartEpoch = nowEpoch;
            saveStateToPreferences();
            
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
            saveStateToPreferences();

            LOG_FERMENTATION("[Fase] Indo para etapa " + String(fermentacaoState.currentStageIndex + 1) + "/" + String(fermentacaoState.totalStages));
        } else {
            LOG_FERMENTATION(F("[Fase] TODAS AS ETAPAS CONCLUÍDAS!"));
            LOG_FERMENTATION(F("[Fase] Mantendo última temperatura até comando manual"));
            
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