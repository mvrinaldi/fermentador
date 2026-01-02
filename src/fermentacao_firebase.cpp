#define ENABLE_DATABASE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "firebase_conexao.h"
#include "fermentacao_firebase.h"
#include "globais.h"
#include "eeprom_layout.h"  // ← Layout unificado da EEPROM

// Variáveis de controle
unsigned long lastActiveCheck = 0;
String lastActiveId = "";
bool isFirstCheck = true;
bool listenerSetup = false;
unsigned long lastListenerCheck = 0;

// Controle de fases
unsigned long stageStartTime = 0;
bool stageStarted = false;
float rampStartTemp = 0;
unsigned long rampStartTime = 0;

// Função para salvar estado na EEPROM - SEÇÃO DE FERMENTAÇÃO (64-127)
void saveStateToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    // Salva ID ativo (bytes 64-95)
    // Alterado de 'int i' para 'unsigned int i' para evitar o warning de signedness
    for (unsigned int i = 0; i < 32; i++) { 
        EEPROM.write(ADDR_ACTIVE_ID + i,
                    i < fermentacaoState.activeId.length() ?
                    fermentacaoState.activeId[i] : 0);
    }

    // Salva índice da etapa (bytes 96-99)
    EEPROM.put(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);

    // Salva tempo de início da etapa (bytes 100-107)
    unsigned long long startMillis = stageStartTime;
    EEPROM.put(ADDR_STAGE_START_TIME, startMillis);

    // Salva flag de etapa iniciada (byte 108)
    EEPROM.put(ADDR_STAGE_STARTED_FLAG, stageStarted);

    // Flag de configuração salva (byte 109)
    EEPROM.write(ADDR_CONFIG_SAVED, 1);

    EEPROM.commit();
    Serial.println(F("[EEPROM] ✅ Estado salvo (Seção Fermentação: 64-127)"));
}

// Função para carregar estado da EEPROM
void loadStateFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Verifica se há dados salvos (byte 109)
    if (EEPROM.read(ADDR_CONFIG_SAVED) != 1) {
        Serial.println(F("[EEPROM] Nenhum estado de fermentação salvo"));
        return;
    }
    
    // Carrega ID ativo (bytes 64-95)
    char idBuffer[32];
    for (int i = 0; i < 32; i++) {
        idBuffer[i] = EEPROM.read(ADDR_ACTIVE_ID + i);
    }
    fermentacaoState.activeId = String(idBuffer);
    
    // Carrega índice da etapa (bytes 96-99)
    EEPROM.get(ADDR_STAGE_INDEX, fermentacaoState.currentStageIndex);
    
    // Carrega tempo de início (bytes 100-107)
    unsigned long long startMillis;
    EEPROM.get(ADDR_STAGE_START_TIME, startMillis);
    stageStartTime = (unsigned long)startMillis;
    
    // Carrega flag de etapa iniciada (byte 108)
    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);
    
    Serial.printf("[EEPROM] ✅ Estado recuperado:\n");
    Serial.printf("   ID: %s\n", fermentacaoState.activeId.c_str());
    Serial.printf("   Etapa: %d\n", fermentacaoState.currentStageIndex);
    Serial.printf("   Iniciada: %s\n", stageStarted ? "SIM" : "NÃO");
    Serial.printf("   Timestamp: %lu\n", stageStartTime);
}

// Função para limpar EEPROM - Apenas seção de fermentação
void clearEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Limpa apenas a seção de fermentação (64-127)
    for (int i = ADDR_FERMENTATION_START; i <= 127; i++) {
        EEPROM.write(i, 0);
    }
    
    EEPROM.commit();
    Serial.println(F("[EEPROM] 🧹 Seção de fermentação limpa (bytes 64-127)"));
}

// ===============================================
// FUNÇÕES DE CONTROLE
// ===============================================

// Atualiza temperatura alvo
void updateTargetTemperature(float temp) {
    fermentacaoState.tempTarget = temp;
    state.targetTemp = temp;
}

// Desativa fermentação atual
void deactivateCurrentFermentation() {
    Serial.println(F("[Firebase] 🧹 Limpando configuração ativa..."));
    
    fermentacaoState.clear();
    lastActiveId = "";
    stageStartTime = 0;
    stageStarted = false;
    updateTargetTemperature(20.0);
    clearEEPROM();
    
    Serial.println(F("[Firebase] ✅ Configuração limpa."));
}

// Configura listener
void setupActiveListener() {
    if (listenerSetup) return;
    Serial.println(F("[Firebase] Monitoramento ativo"));
    listenerSetup = true;
    loadStateFromEEPROM(); // Recupera estado ao iniciar
}

// Mantém listener ativo
void keepListenerAlive() {
    unsigned long now = millis();
    if (now - lastListenerCheck >= 60000) {
        lastListenerCheck = now;
        getTargetFermentacao();
    }
}

// Para listener
void stopActiveListener() {
    listenerSetup = false;
    Serial.println(F("[Firebase] Monitoramento desabilitado."));
}

// ===============================================
// BUSCA FERMENTAÇÃO ATIVA NO FIREBASE
// ===============================================
void getTargetFermentacao() {
    unsigned long now = millis();
    
    if (!isFirstCheck && (now - lastActiveCheck < ACTIVE_CHECK_INTERVAL)) {
        return;
    }
    
    lastActiveCheck = now;
    Serial.println(F("[Firebase] Buscando fermentação ativa..."));

    String result = Database.get<String>(aClient, "/active");
    
    if (result.length() > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, result)) {
            Serial.println(F("[Firebase] Erro ao parsear JSON."));
            return;
        }
        
        bool active = doc["active"] | false;
        String idFermentacao = doc["id"] | "";
        
        Serial.printf("[Firebase] Status: %s, ID: %s\n", 
                     active ? "ATIVA" : "INATIVA", idFermentacao.c_str());
        
        if (active && idFermentacao.length() > 0) {
            if (idFermentacao != lastActiveId) {
                Serial.println(F("[Firebase] 🎯 Nova fermentação ativada!"));
                
                fermentacaoState.active = true;
                fermentacaoState.activeId = idFermentacao;
                fermentacaoState.currentStageIndex = 0;
                strlcpy(fermentacaoState.configName, idFermentacao.c_str(), 
                       sizeof(fermentacaoState.configName));
                lastActiveId = idFermentacao;
                
                // Carrega configuração completa
                loadConfigParameters(idFermentacao);
                
                // Reseta início de etapa
                stageStartTime = 0;
                stageStarted = false;
                
                saveStateToEEPROM();
            }
        } else if (fermentacaoState.active && !active) {
            deactivateCurrentFermentation();
        } else {
            fermentacaoState.clear();
            updateTargetTemperature(20.0);
        }
    } else {
        Serial.println(F("[Firebase] Nenhuma resposta do Firebase."));
    }
    
    isFirstCheck = false;
}

// Carrega todos os parâmetros e etapas da configuração
void loadConfigParameters(const String& configId) {
    if (configId.isEmpty()) return;
    
    String path = "/configurations/" + configId;
    Serial.printf("[Firebase] Carregando configuração completa: %s\n", path.c_str());
    
    String result = Database.get<String>(aClient, path.c_str());
    
    if (result.length() > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, result)) {
            Serial.println(F("[Firebase] Erro ao parsear configuração."));
            return;
        }
        
        const char* name = doc["name"] | "Sem nome";
        int currentStageIndex = doc["currentStageIndex"] | 0;
        
        fermentacaoState.currentStageIndex = currentStageIndex;
        strlcpy(fermentacaoState.configName, name, sizeof(fermentacaoState.configName));
        
        Serial.printf("[Firebase] 📋 Configuração: %s\n", name);
        
        // Carrega todas as etapas
        JsonArray stages = doc["stages"];
        int stageCount = 0;
        
        for (JsonVariant stage : stages) {
            if (stageCount >= MAX_STAGES) break;
            FermentationStage& s = fermentacaoState.stages[stageCount];

            const char* type = stage["type"] | "temperature";
            s.targetTemp = stage["targetTemp"] | 20.0;
            
            // CORREÇÃO: Mapeando startTemp que existe no seu Firebase
            s.startTemp = stage["startTemp"] | 20.0; 

            // CORREÇÃO: Firebase usa "rampTime", ESP usava "rampTimeHours" ou "durationDays"
            s.rampTimeHours = stage["rampTime"] | 0; 
            
            // Se for uma etapa comum (não rampa), você pode precisar de durationDays
            s.durationDays = stage["durationDays"] | 0; 

            s.targetGravity = stage["targetGravity"] | 0.0;
            s.timeoutDays = stage["timeoutDays"] | 0;
            
            // Define tipo
            if (strcmp(type, "temperature") == 0) { 
                s.type = STAGE_TEMPERATURE;
            } else if (strcmp(type, "ramp") == 0) {
                s.type = STAGE_RAMP;
            } else if (strcmp(type, "gravity") == 0) {
                s.type = STAGE_GRAVITY;
            } else if (strcmp(type, "gravity_time") == 0) {
                s.type = STAGE_GRAVITY_TIME;
            }
            
            Serial.printf("  Etapa %d: %s | Temp=%.1f°C | Duração=%d dias\n", 
                         stageCount + 1, type, s.targetTemp, s.durationDays);
            
            stageCount++;
        }
        
        fermentacaoState.totalStages = stageCount;
        Serial.printf("[Firebase] ✅ %d etapas carregadas\n", stageCount);
        
        // Define temperatura da etapa atual
        if (currentStageIndex < stageCount) {
            updateTargetTemperature(fermentacaoState.stages[currentStageIndex].targetTemp);
        }
    }
}

// Atualiza índice de etapa no Firebase
void updateStageIndexInFirebase(int newIndex) {
    if (fermentacaoState.activeId.isEmpty()) return;
    
    String path = "/configurations/" + fermentacaoState.activeId + "/currentStageIndex";
    
    if (Database.set<int>(aClient, path.c_str(), newIndex)) {
        Serial.printf("[Firebase] ✅ Etapa atualizada no Firebase: %d\n", newIndex);
    } else {
        Serial.println(F("[Firebase] ❌ Erro ao atualizar etapa no Firebase"));
    }
}

// Verifica e processa troca de fase (FUNÇÃO PRINCIPAL)
void verificarTrocaDeFase() {
    // 1. Verificações de segurança iniciais
    if (!fermentacaoState.active) return;
    if (fermentacaoState.currentStageIndex >= fermentacaoState.totalStages) {
        Serial.println(F("[Fase] ✅ Todas as etapas concluídas!"));
        return;
    }

    FermentationStage& currentStage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
    unsigned long now = millis();
    float currentTemp = state.currentTemp;

    // 2. Lógica de início da etapa (Gatilho de tempo)
    if (!stageStarted) {
        float tolerance = 0.5; // Definido em TEMPERATURE_TOLERANCE
        
        // Se for RAMPA, o tempo começa imediatamente para permitir a subida gradual
        // Se for outro tipo, aguarda a temperatura do fermentador atingir o alvo
        if (currentStage.type == STAGE_RAMP || abs(currentTemp - currentStage.targetTemp) <= tolerance) {
            stageStartTime = now;
            stageStarted = true;
            Serial.printf("[Fase] 🎯 Iniciando contagem da etapa %d (Tipo: %d)\n", 
                          fermentacaoState.currentStageIndex + 1, currentStage.type);
            saveStateToEEPROM(); // Persiste o início na EEPROM
        } else {
            return; // Aguardando atingir a temperatura para começar a contar
        }
    }

    // 3. Cálculos de tempo decorrido
    unsigned long elapsedMillis = now - stageStartTime;
    float elapsedHours = elapsedMillis / 3600000.0; // Conversão para horas (3.6m ms)
    float elapsedDays = elapsedMillis / 86400000.0;  // Conversão para dias (86.4m ms)
    bool shouldAdvance = false;

    // 4. Lógica de verificação por tipo de estágio
    switch (currentStage.type) {
        case STAGE_TEMPERATURE:
            // Avança quando os dias de duração completarem
            if (elapsedDays >= (float)currentStage.durationDays) {
                Serial.printf("[Fase] ⏰ Etapa %d concluída por tempo (%.2f/ %d dias)\n",
                              fermentacaoState.currentStageIndex + 1, elapsedDays, currentStage.durationDays);
                shouldAdvance = true;
            }
            break;

        case STAGE_RAMP:
            // Rampa baseada em HORAS (campo rampTimeHours do Firebase)
            if (elapsedHours >= (float)currentStage.rampTimeHours) {
                Serial.printf("[Fase] 📈 Rampa %d concluída (%d horas)\n",
                              fermentacaoState.currentStageIndex + 1, currentStage.rampTimeHours);
                shouldAdvance = true;
            } else {
                // Cálculo da temperatura intermediária da rampa
                float tempInicial;
                if (fermentacaoState.currentStageIndex > 0) {
                    // Pega o alvo da etapa anterior como início
                    tempInicial = fermentacaoState.stages[fermentacaoState.currentStageIndex - 1].targetTemp;
                } else {
                    // Se for a primeira etapa, usa o startTemp definido
                    tempInicial = currentStage.startTemp;
                }

                float tempFinal = currentStage.targetTemp;
                // Evita divisão por zero se a rampa for 0 horas
                float divisor = (currentStage.rampTimeHours > 0) ? (float)currentStage.rampTimeHours : 1.0;
                float progresso = elapsedHours / divisor;
                
                float tempRampa = tempInicial + (tempFinal - tempInicial) * progresso;
                updateTargetTemperature(tempRampa); // Atualiza o setpoint do PID 
            }
            break;

        case STAGE_GRAVITY:
            // Avança por densidade do iSpindel
            if (mySpindel.gravity > 0 && mySpindel.gravity <= currentStage.targetGravity) {
                Serial.printf("[Fase] 🎯 Etapa %d concluída por gravidade (%.3f)\n",
                              fermentacaoState.currentStageIndex + 1, mySpindel.gravity);
                shouldAdvance = true;
            }
            break;

        case STAGE_GRAVITY_TIME:
            // Avança por gravidade OU timeout em dias
            if (mySpindel.gravity > 0 && mySpindel.gravity <= currentStage.targetGravity) {
                Serial.printf("[Fase] 🎯 Etapa %d concluída por gravidade (%.3f)\n",
                              fermentacaoState.currentStageIndex + 1, mySpindel.gravity);
                shouldAdvance = true;
            } else if (elapsedDays >= (float)currentStage.timeoutDays) {
                Serial.printf("[Fase] ⏰ Etapa %d concluída por timeout (%.1f dias)\n",
                              fermentacaoState.currentStageIndex + 1, elapsedDays);
                shouldAdvance = true;
            }
            break;
    }

    // 5. Avanço de etapa e sincronização 
    if (shouldAdvance) {
        fermentacaoState.currentStageIndex++;

        if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
            // Prepara a próxima etapa 
            FermentationStage& nextStage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
            updateTargetTemperature(nextStage.targetTemp);
            
            Serial.printf("[Fase] ➡️ Avançando para etapa %d/%d (Alvo: %.1f°C)\n",
                          fermentacaoState.currentStageIndex + 1,
                          fermentacaoState.totalStages,
                          nextStage.targetTemp);

            // Reseta flags para o próximo estágio
            stageStartTime = 0;
            stageStarted = false;
            
            // Atualiza nuvem e memória local
            updateStageIndexInFirebase(fermentacaoState.currentStageIndex);
            saveStateToEEPROM();
        } else {
            // Finalização total do processo
            Serial.println(F("[Fase] 🎉 FERMENTAÇÃO CONCLUÍDA!"));
            deactivateCurrentFermentation(); // Limpa estado e EEPROM
        }
    }
}