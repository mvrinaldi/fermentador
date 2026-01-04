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

// Controle de fases
unsigned long stageStartTime = 0;
bool stageStarted = false;

// =====================================================
// FUNÇÕES AUXILIARES LOCAIS
// =====================================================

// Função segura para copiar strings
static void safe_strcpy(char* dest, const char* src, size_t destSize) {
    if (!dest || destSize == 0) return;
    
    if (src) {
        strncpy(dest, src, destSize - 1);
        dest[destSize - 1] = '\0';
    } else {
        dest[0] = '\0';
    }
}

// Verifica se uma string é válida (não nula e não vazia)
static bool isValidString(const char* str) {
    return str && str[0] != '\0';
}

// =====================================================
// EEPROM - FUNÇÕES SEGURAS
// =====================================================
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    // Salva ID ativo (32 bytes reservados, garante terminação)
    eepromWriteString(fermentacaoState.activeId, ADDR_ACTIVE_ID, sizeof(fermentacaoState.activeId));

    // Salva o índice da etapa (4 bytes)
    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    // Salva timestamp (4 bytes)
    unsigned long startMillis = (unsigned long)stageStartTime;
    EEPROM.put(ADDR_STAGE_START_TIME, startMillis);

    // Salva flags
    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);
    EEPROM.write(ADDR_CONFIG_SAVED, 1);
    
    // Commit das alterações
    if (!EEPROM.commit()) {
        Serial.println(F("[EEPROM] ❌ Falha ao salvar estado"));
    } else {
        Serial.println(F("[EEPROM] ✅ Estado salvo com sucesso"));
    }
}

void loadStateFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    // Verifica se existe uma configuração válida salva
    if (EEPROM.read(ADDR_CONFIG_SAVED) != 1) {
        Serial.println(F("[EEPROM] Nenhum estado salvo"));
        return;
    }

    // Restaura o ID Ativo usando função segura
    eepromReadString(fermentacaoState.activeId, 
                     sizeof(fermentacaoState.activeId), 
                     ADDR_ACTIVE_ID, 
                     sizeof(fermentacaoState.activeId));

    // CORREÇÃO: Verifica se o ID restaurado é válido
    if (!isValidString(fermentacaoState.activeId)) {
        Serial.println(F("[EEPROM] ⚠️  ID inválido restaurado, limpando..."));
        clearEEPROM();
        fermentacaoState.clear();
        return;
    }

    // Restaura o índice da etapa atual
    EEPROM.get(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    // Restaura timestamp
    unsigned long startMillis;
    EEPROM.get(ADDR_STAGE_START_TIME, startMillis);
    stageStartTime = startMillis;

    // Restaura flag de etapa iniciada
    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);

    // Define o estado ativo baseado na existência de um ID válido
    fermentacaoState.active = isValidString(fermentacaoState.activeId);

    // CORREÇÃO IMPORTANTE: Se o sistema estiver ativo mas não houver ID válido, limpa
    if (fermentacaoState.active && !isValidString(fermentacaoState.activeId)) {
        Serial.println(F("[EEPROM] ⚠️  Estado inconsistente: ativo sem ID, limpando..."));
        clearEEPROM();
        fermentacaoState.clear();
        fermentacaoState.tempTarget = 20.0;
        state.targetTemp = 20.0;
        return;
    }

    Serial.print(F("[EEPROM] ✅ Estado restaurado: ID="));
    Serial.println(fermentacaoState.activeId);
}

void clearEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Limpa apenas a seção de fermentação (64-127)
    for (int i = ADDR_FERMENTATION_START; i <= 127; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        Serial.println(F("[EEPROM] ✅ Seção de fermentação limpa"));
    } else {
        Serial.println(F("[EEPROM] ❌ Falha ao limpar EEPROM"));
    }
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

    // Respeita o intervalo de verificação (30s)
    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }

    lastActiveCheck = now;
    Serial.println(F("[Firebase] Buscando fermentação ativa"));


    String path = String("/active");
    String result = Database.get<String>(aClient, path.c_str());

    if (result.isEmpty()) {
        Serial.println(F("[Firebase] Sem resposta"));
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, result);
    
    if (error) {
        Serial.print(F("[Firebase] JSON inválido: "));
        Serial.println(error.c_str());
        return;
    }

    bool active = doc["active"] | false;
    const char* id = doc["id"] | "";

    // Verifica se o ID é válido
    if (!isValidString(id)) {
        id = "";
    }

    if (active && isValidString(id)) {
        // Verifica se houve mudança de ID ou estado
        if (strcmp(id, lastActiveId) != 0) {
            Serial.println(F("[Firebase] 🎯 Nova fermentação detectada"));

            fermentacaoState.active = true;
            
            // Copia o ID de forma segura
            safe_strcpy(fermentacaoState.activeId, id, sizeof(fermentacaoState.activeId));
            
            fermentacaoState.currentStageIndex = 0;

            // Atualiza o ID de controle
            safe_strcpy(lastActiveId, id, sizeof(lastActiveId));

            // Carrega os parâmetros específicos desta configuração
            loadConfigParameters(id);

            // Reseta timers de fase
            stageStartTime = 0;
            stageStarted = false;
            fermentacaoState.targetReachedSent = false;

            // Salva o novo estado imediatamente na EEPROM
            saveStateToEEPROM();
            
            Serial.print(F("[Firebase] Nova ID configurada: "));
            Serial.println(id);
        }
    } else if (fermentacaoState.active && !active) {
        // Se a fermentação foi desativada no Firebase, limpa o estado local
        Serial.println(F("[Firebase] Fermentação desativada remotamente"));
        deactivateCurrentFermentation();
    } else if (!active && fermentacaoState.active) {
        // Se localmente está ativo mas remotamente não, mantém o estado local
        // (permite operação offline)
        Serial.println(F("[Firebase] Modo offline - mantendo estado local"));
    }

    isFirstCheck = false;
}

// =====================================================
// CONFIGURAÇÃO DE ETAPAS
// =====================================================
void loadConfigParameters(const char* configId) {
    if (!configId || strlen(configId) == 0) {
        Serial.println(F("[Firebase] ❌ ID de configuração inválido"));
        return;
    }

    // Buscar apenas os campos necessários em vez de tudo
    String path = String("/configurations/") + configId;
    
    // Primeiro, busca os campos básicos para verificar
    String basicPath = path + "?shallow=true";
    Serial.printf("[Firebase] 🔧 Buscando campos básicos: %s\n", basicPath.c_str());
    
    String result = Database.get<String>(aClient, basicPath.c_str());
    
    if (result.isEmpty()) {
        Serial.println(F("[Firebase] ❌ Não encontrou configuração"));
        return;
    }

    // Agora busca apenas os campos necessários
    String targetPath = path + "/name,currentStageIndex,stages";
    Serial.printf("[Firebase] 🔧 Buscando campos específicos: %s\n", targetPath.c_str());
    
    result = Database.get<String>(aClient, targetPath.c_str());
    
    if (result.isEmpty()) {
        Serial.println(F("[Firebase] ❌ Configuração vazia"));
        return;
    }

    Serial.printf("[Firebase] ✅ Resposta recebida (%d bytes)\n", result.length());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, result);
    
    if (error) {
        Serial.print(F("[Firebase] ❌ JSON inválido: "));
        Serial.println(error.c_str());
        return;
    }

    // Processa os dados
    fermentacaoState.currentStageIndex = doc["currentStageIndex"] | 0;
    
    const char* name = doc["name"] | "Sem nome";
    fermentacaoState.setConfigName(name);
    
    // Processa as etapas
    JsonArray stages = doc["stages"];
    int count = 0;
    
    for (JsonVariant stage : stages) {
        if (count >= MAX_STAGES) {
            Serial.println(F("[Firebase] ⚠️  Número máximo de etapas excedido"));
            break;
        }

        FermentationStage& s = fermentacaoState.stages[count];
        
        // Determina tipo da etapa
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

        // Carrega parâmetros (ajuste conforme sua estrutura atual)
        s.targetTemp = stage["targetTemp"] | 20.0;
        s.startTemp = stage["startTemp"] | 20.0;
        s.rampTimeHours = stage["rampTime"] | 0;
        s.durationDays = stage["duration"] | 0;
        s.targetGravity = stage["targetGravity"] | 0.0;
        s.timeoutDays = stage["timeoutDays"] | 0;
        s.startTime = 0;
        s.completed = false;

        count++;
    }

    fermentacaoState.totalStages = count;

    // Atualiza temperatura alvo
    if (count > 0 && fermentacaoState.currentStageIndex < count) {
        float targetTemp = fermentacaoState.stages[fermentacaoState.currentStageIndex].targetTemp;
        updateTargetTemperature(targetTemp);
        Serial.printf("[Firebase] 🌡️  Temperatura alvo: %.1f°C\n", targetTemp);
    }

    // Envia os timers das etapas
    sendStageTimers();
    
    Serial.printf("[Firebase] ✅ Configuração carregada: %d etapas\n", count);
}

// =====================================================
// TROCA DE FASE
// =====================================================
// =====================================================
// TROCA DE FASE (VERSÃO CORRIGIDA)
// =====================================================
void verificarTrocaDeFase() {
    if (!fermentacaoState.active) return;
    
    // CORREÇÃO: Verifica se totalStages é 0 e desativa a fermentação
    if (fermentacaoState.totalStages == 0) {
        Serial.println(F("[Fase] ⚠️  Configuração inválida: 0 etapas"));
        Serial.println(F("[Fase] 🧹 Desativando fermentação..."));
        deactivateCurrentFermentation();
        return;
    }
    
    // Verificação original do índice
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        Serial.println(F("[Fase] ⚠️  Índice de etapa inválido"));
        
        // Se o índice é inválido mas temos etapas, corrige o índice
        if (fermentacaoState.totalStages > 0) {
            fermentacaoState.currentStageIndex = 0;
            Serial.println(F("[Fase] 🔄 Recomeçando da etapa 0"));
        } else {
            // Se não há etapas, desativa
            deactivateCurrentFermentation();
        }
        return;
    }

    FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
    unsigned long now = millis();

    // Inicia etapa se necessário
    if (!stageStarted) {
        stageStartTime = now;
        stageStarted = true;
        fermentacaoState.targetReachedSent = false;
        
        // Salva estado inicial
        saveStateToEEPROM();
        
        Serial.printf("[Fase] ▶️  Etapa %d iniciada\n", fermentacaoState.currentStageIndex);
    }

    // Calcula tempo decorrido
    float elapsedH = (now - stageStartTime) / 3600000.0;
    float elapsedD = (now - stageStartTime) / 86400000.0;

    // Processa etapa atual
    bool stageCompleted = processCurrentStage(stage, elapsedD, elapsedH);

    if (stageCompleted) {
        Serial.printf("[Fase] ✅ Etapa %d concluída\n", fermentacaoState.currentStageIndex);
        
        fermentacaoState.currentStageIndex++;
        stageStarted = false;
        stageStartTime = 0;

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            // Avança para próxima etapa
            FermentationStage& next = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            updateTargetTemperature(next.targetTemp);
            saveStateToEEPROM();
            
            Serial.printf("[Fase] ↪️  Indo para etapa %d (%.1f°C)\n", 
                         fermentacaoState.currentStageIndex, next.targetTemp);
        } else {
            // Todas as etapas concluídas
            Serial.println(F("[Fase] 🎉 Todas as etapas concluídas"));
            deactivateCurrentFermentation();
        }
    }
}

void verificarTargetAtingido() {
    // Só verifica se a fermentação está ativa e se ainda não enviamos nesta fase
    if (!fermentacaoState.active || fermentacaoState.targetReachedSent) return;

    // Calcula a diferença entre temperatura atual e alvo
    float diff = abs(state.currentTemp - fermentacaoState.tempTarget);

    if (diff <= TEMPERATURE_TOLERANCE) {
        JsonDocument doc;
        doc["targetReachedTime"][".sv"] = "timestamp";
        
        // Envia para o nó de estado
        String payload;
        serializeJson(doc, payload);
        
        if (Database.update(aClient, "/fermentationState", payload)) {
            fermentacaoState.targetReachedSent = true;
            Serial.println(F("[Firebase] 🎯 Temperatura alvo atingida!"));
        } else {
            Serial.println(F("[Firebase] ❌ Falha ao enviar notificação"));
        }
    }
}

// =====================================================
// LEITURAS
// =====================================================
void enviarLeituraAtual() {
    if (!fermentacaoState.active || !isValidString(fermentacaoState.activeId)) {
        return;
    }

    JsonDocument doc;
    doc["tempFridge"] = sensors.getTempCByIndex(1);
    doc["tempFermenter"] = state.currentTemp;
    doc["gravity"] = mySpindel.gravity;
    doc["tempTarget"] = fermentacaoState.tempTarget;
    doc["timestamp"][".sv"] = "timestamp";

    String path = String("/readings/") + fermentacaoState.activeId;
    String payload;
    serializeJson(doc, payload);
    
    if (Database.push(aClient, path.c_str(), payload)) {
        Serial.println(F("[Firebase] 📊 Leitura enviada"));
    } else {
        Serial.println(F("[Firebase] ❌ Falha ao enviar leitura"));
    }
}