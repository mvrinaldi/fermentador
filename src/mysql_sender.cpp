// mysql_sender.cpp - Módulo de envio de dados para MySQL
// ✅ CONSOLIDADO: Removidas funções duplicadas
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include "mysql_sender.h"
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
#include "ispindel_struct.h"

extern FermentadorHTTPClient httpClient;

// =====================================================
// FUNÇÕES AUXILIARES PARA timeRemaining
// =====================================================

void formatTimeRemaining(JsonObject& timeRemaining, float remainingH, const char* status) {
    if (remainingH >= 24.0) {
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
        int minutes = roundf(remainingH * 60.0);
        timeRemaining["value"] = minutes;
        timeRemaining["unit"] = "minutes";
    }
    
    // Adiciona status se fornecido
    if (status && strlen(status) > 0) {
        timeRemaining["status"] = status;
    }
}

// =====================================================
// COMPRESSÃO DE DADOS
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
    
    // ========== 4. Comprimir timeRemaining ==========
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
            } else if (strcmp(status, MSG_TC) == 0) {
                compactTR.add(MSG_TC);  // "tc" = time completed
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
            } else if (strcmp(unit, "completed") == 0) {
                compactTR.add(MSG_TC);  // "tc" para fermentação concluída
            } else {
                compactTR.add(unit);
            }
            
            if (strcmp(status, "running") == 0) {
                compactTR.add(MSG_RUN);
            } else if (strcmp(status, "waiting") == 0) {
                compactTR.add(MSG_WAIT);
            } else if (strcmp(status, "waiting_gravity") == 0) {
                compactTR.add(WG);
            } else if (strcmp(status, MSG_TC) == 0) {
                compactTR.add(MSG_TC);  // "tc" = time completed
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
    };
    
    for (const auto& mapping : fieldMappings) {
        if (!doc[mapping.longName].isNull()) {
            doc[mapping.shortName] = doc[mapping.longName];
            doc.remove(mapping.longName);
        }
    }
    
    // ========== 6. Tratar targetReached ==========
    if (!doc["targetReached"].isNull()) {
        if (trCreatedFromTimeRemaining) {
            doc.remove("targetReached");
            #if DEBUG_ENVIODADOS
            Serial.println(F("[Compress] targetReached removido (já temos tr array)"));
            #endif
        } else {
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
// ENVIAR ESTADO COMPLETO (FUNÇÃO PRINCIPAL)
// Envia: status, etapa, temperaturas, controle, timeRemaining
// Intervalo: 30 segundos
// =====================================================
void enviarEstadoCompletoMySQL() {
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
    
    // ========== CONTROLE DE TEMPO (30 segundos) ==========
    static unsigned long lastStateSend = 0;
    unsigned long now = millis();
    
    if (now - lastStateSend < 30000) {
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
        
        // ✅ NOVO: Envia timeRemaining com "tc" para indicar fermentação concluída
        JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
        timeRemaining["value"] = 0;
        timeRemaining["unit"] = "completed";
        timeRemaining["status"] = MSG_TC;  // "tc" = time completed
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
    if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages && 
        fermentacaoState.targetReachedSent) {
        
        FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
        JsonObject timeRemaining = doc["timeRemaining"].to<JsonObject>();
        
        if (fermentacaoState.stageStartEpoch > 0) {
            time_t nowEpoch = getCurrentEpoch();
            
            if (nowEpoch > 0) {
                float elapsedH = difftime(nowEpoch, fermentacaoState.stageStartEpoch) / 3600.0f;
                float totalH = 0.0f;
                
                switch (stage.type) {
                    case STAGE_TEMPERATURE:
                        totalH = stage.holdTimeHours;
                        break;
                    case STAGE_RAMP:
                        totalH = (float)stage.rampTimeHours;
                        break;
                    case STAGE_GRAVITY_TIME:
                        totalH = stage.maxTimeHours;
                        break;
                    default:
                        totalH = 0.0f;
                }
                
                float remainingH = totalH - elapsedH;
                if (remainingH < 0) remainingH = 0;
                
                formatTimeRemaining(timeRemaining, remainingH, "running");
            }
        } else {
            switch (stage.type) {
                case STAGE_TEMPERATURE:
                    formatTimeRemaining(timeRemaining, stage.holdTimeHours, "running");
                    break;
                case STAGE_RAMP:
                    formatTimeRemaining(timeRemaining, (float)stage.rampTimeHours, "running");
                    break;
                case STAGE_GRAVITY_TIME:
                    formatTimeRemaining(timeRemaining, stage.maxTimeHours, "running");
                    break;
                case STAGE_GRAVITY:
                    timeRemaining["value"] = 0;
                    timeRemaining["unit"] = "indefinite";
                    timeRemaining["status"] = "waiting_gravity";
                    break;
            }
        }
    }
    
    // ========== 4. STATUS DO BREWPI (CONTROLE) ==========
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
    Serial.printf("[DEBUG] Heap livre: %d bytes\n", ESP.getFreeHeap());
    #else
    httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    #endif
    
    // ✅ Log de estado de controle (antes era no main.cpp a cada 30s)
    LOG_MAIN("[HTTP] ✅ Estado completo enviado (30s interval)");
    LOG_MAIN("[DEBUG] State: " + String(detailedStatus.stateName) + 
        ", Cooler: " + (detailedStatus.coolerActive ? "ON" : "OFF") +
        ", Heater: " + (detailedStatus.heaterActive ? "ON" : "OFF"));
    
    if (detailedStatus.isWaiting && detailedStatus.waitReason) {
        LOG_MAIN("[DEBUG] Wait: " + String(detailedStatus.waitReason) + 
                 " (" + String(detailedStatus.waitTimeRemaining) + "s)");
    }
}

// =====================================================
// ENVIAR LEITURAS DOS SENSORES (HISTÓRICO)
// Envia: temp_fridge, temp_fermenter, temp_target, gravity
// Intervalo: 60 segundos
// =====================================================
void enviarLeiturasSensoresMySQL() {
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

    httpClient.sendReading(
        fermentacaoState.activeId, 
        tempFridge, 
        tempFermenter, 
        fermentacaoState.tempTarget, 
        mySpindel.gravity,
        mySpindel.temperature,
        mySpindel.battery
    );
    
    #if DEBUG_ENVIODADOS
    char logBuf[128];
    snprintf(logBuf, sizeof(logBuf), 
        "[Envio] F=%.1f G=%.1f Grav=%.3f ST=%.1f SB=%.2f", 
        tempFermenter, tempFridge, mySpindel.gravity, 
        mySpindel.temperature, mySpindel.battery);
    Serial.println(logBuf);
    #endif
}

// =====================================================
// ENVIAR HEARTBEAT SIMPLIFICADO
// Envia apenas: uptime, free_heap (saúde do sistema)
// Dados de controle já estão em enviarEstadoCompletoMySQL
// =====================================================
bool sendHeartbeatMySQL(int configId) {
    if (!httpClient.isConnected() || configId <= 0) {
        return false;
    }
    
    // Controle de tempo interno (30 segundos)
    static unsigned long lastHeartbeat = 0;
    unsigned long now = millis();
    if (now - lastHeartbeat < 30000) {
        return true; // Não é erro, apenas ainda não é hora
    }
    lastHeartbeat = now;
    
    JsonDocument doc;
    
    // Dados de saúde do sistema (simplificado)
    doc["config_id"] = configId;
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    
    // Temperaturas básicas (para monitoramento)
    temperature beerTemp = brewPiControl.getBeerTemp();
    temperature fridgeTemp = brewPiControl.getFridgeTemp();
    
    if (beerTemp != INVALID_TEMP) doc["temp_fermenter"] = tempToFloat(beerTemp);
    if (fridgeTemp != INVALID_TEMP) doc["temp_fridge"] = tempToFloat(fridgeTemp);
    
    // NÃO envia estado de controle - já está em enviarEstadoCompletoMySQL
    
    String response;
    bool result = httpClient.sendHeartbeat(configId, brewPiControl.getDetailedStatus(), 
                                            beerTemp, fridgeTemp);
    
    #if DEBUG_HEARTBEAT
    if (result) {
        Serial.println(F("[MySQL] ✅ Heartbeat enviado (simplificado)"));
    }
    #endif
    
    return result;
}

// =====================================================
// ENVIAR LISTA DE SENSORES DETECTADOS
// =====================================================
bool sendSensorsDataMySQL(const JsonDocument& doc) {
    if (!httpClient.isConnected()) {
        #if DEBUG_SENSORES
        Serial.println(F("[MySQL] ❌ HTTP não conectado"));
        #endif
        return false;
    }
    
    bool success = httpClient.sendSensorsData(doc);
    
    #if DEBUG_SENSORES
    if (success) {
        Serial.println(F("[MySQL] ✅ Sensores enviados"));
    } else {
        Serial.println(F("[MySQL] ❌ Erro ao enviar sensores"));
    }
    #endif
    
    return success;
}

// =====================================================
// ENVIAR DADOS ISPINDEL PARA MYSQL
// =====================================================
bool sendISpindelDataMySQL(const JsonDocument& doc) {
    if (!httpClient.isConnected()) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ❌ HTTP não conectado para iSpindel"));
        #endif
        return false;
    }
    
    WiFiClient wifiClient;
    HTTPClient httpMySQL;
    
    String mysqlUrl = String(SERVER_URL) + "ispindel/data";
    
    if (!httpMySQL.begin(wifiClient, mysqlUrl)) {
        #if DEBUG_FERMENTATION
        Serial.println(F("[MySQL] ❌ Falha ao iniciar conexão iSpindel"));
        #endif
        return false;
    }
    
    httpMySQL.setTimeout(HTTP_TIMEOUT);
    httpMySQL.addHeader("Content-Type", "application/json");
    
    String payload;
    serializeJson(doc, payload);
    
    int code = httpMySQL.POST(payload);
    bool success = (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED);
    
    #if DEBUG_FERMENTATION
    if (success) {
        Serial.println(F("[MySQL] ✅ Dados iSpindel enviados"));
    } else {
        Serial.printf("[MySQL] ❌ Erro iSpindel: %d\n", code);
    }
    #endif
    
    httpMySQL.end();
    return success;
}

// =====================================================
// ENVIAR RESUMO DAS ETAPAS
// =====================================================
void sendStagesSummaryMySQL() {
    if (!httpClient.isConnected()) return;

    JsonDocument doc;
    doc["totalStages"] = fermentacaoState.totalStages;
    doc["currentStageIndex"] = fermentacaoState.currentStageIndex;

    JsonArray stagesArray = doc["stages"].to<JsonArray>();

    for (int i = 0; i < fermentacaoState.totalStages; i++) {
        const FermentationStage& stage = fermentacaoState.stages[i];
        JsonObject stageObj = stagesArray.add<JsonObject>();
        stageObj["index"] = i;
        stageObj["targetTemp"] = stage.targetTemp;

        switch (stage.type) {
            case STAGE_TEMPERATURE: 
                stageObj["type"] = "temperature"; 
                stageObj["duration"] = stage.durationDays;
                break;
            case STAGE_RAMP:        
                stageObj["type"] = "ramp"; 
                stageObj["durationHours"] = stage.rampTimeHours;
                break;
            case STAGE_GRAVITY:     
                stageObj["type"] = "gravity"; 
                stageObj["targetGravity"] = stage.targetGravity;
                break;
            case STAGE_GRAVITY_TIME:
                stageObj["type"] = "gravity_time";
                stageObj["targetGravity"] = stage.targetGravity;
                stageObj["timeout"] = stage.timeoutDays;
                break;
        }
    }

    httpClient.updateFermentationState(fermentacaoState.activeId, doc);
    
    #if DEBUG_FERMENTATION
    Serial.println(F("[MySQL] 📋 Resumo de etapas enviado"));
    #endif
}