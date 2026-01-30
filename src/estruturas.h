// estruturas.h
#pragma once
#include <Arduino.h>
#include <time.h>  // Para time_t

// Importa apenas para usar MAX_STAGES
#include "definitions.h"

// === Estrutura de Relé === //
struct Rele {
  uint8_t pino;
  bool estado;
  bool invertido;
  char nome[16];
  
  void atualizar() {
    int pinValue = invertido ? !estado : estado;
    digitalWrite(pino, pinValue);
    
    // ✅ DEBUG temporário
    #if DEBUG_RELES
    Serial.printf("[RELE] %s: estado=%d, invertido=%d, pino=%d, valor=%d\n",
                  nome, estado, invertido, pino, pinValue);
    #endif
  }
};

// === Estado Geral do Sistema === //
struct SystemState {
  float currentTemp;
  float targetTemp;
  unsigned long lastUpdate;
  char configName[20];
  bool active;
  unsigned long lastTempUpdate;  // ✅ NOVO: Timestamp da última atualização de temperatura
  
  SystemState() : currentTemp(0.0), targetTemp(20.0), lastUpdate(0), active(false), lastTempUpdate(0) {
    configName[0] = '\0';
  }
};

// === Configuração Local === //
struct LocalConfig {
  float targetTemp;
  float hysteresis;
  bool useHTTP;
  
  LocalConfig() : targetTemp(20.0), hysteresis(0.5), useHTTP(true) {}
};

// === Informação de Sensor === //
struct SensorInfo {
  char nome[20];
  char endereco[17];
  
  SensorInfo() {
    nome[0] = '\0';
    endereco[0] = '\0';
  }
};

// === Tipos de Etapa === //
enum StageType {
    STAGE_TEMPERATURE,
    STAGE_RAMP,
    STAGE_GRAVITY,
    STAGE_GRAVITY_TIME
};

struct FermentationStage {
    StageType type;
    float targetTemp;
    float startTemp;
    float durationDays;
    int rampTimeHours;
    float targetGravity;
    float timeoutDays;
    
    // Campos calculados (preenchidos em loadConfigParameters)
    float holdTimeHours;    // = durationDays * 24
    float maxTimeHours;     // = timeoutDays * 24
    
    time_t startTime;
    bool completed;
    
    // Construtor padrão
    FermentationStage() : 
        type(STAGE_TEMPERATURE),
        targetTemp(20.0f),
        startTemp(20.0f),
        durationDays(0.0f),
        rampTimeHours(0),
        targetGravity(0.0f),
        timeoutDays(0.0f),
        holdTimeHours(0.0f),
        maxTimeHours(0.0f),
        startTime(0),
        completed(false) {}
};

// === Estado da Fermentação === //
struct FermentacaoState {
    bool active;
    bool concluidaMantendoTemp;
    char activeId[32];
    char configName[64];
    float tempTarget;
    int currentStageIndex;
    bool targetReachedSent;
    int totalStages;
    FermentationStage stages[MAX_STAGES];
    unsigned long lastUpdate;
    time_t stageStartEpoch;
    
    FermentacaoState() : 
        active(false),
        concluidaMantendoTemp(false),
        tempTarget(DEFAULT_TEMPERATURE),
        currentStageIndex(0),
        targetReachedSent(false),
        totalStages(0),
        lastUpdate(0),
        stageStartEpoch(0) {
        activeId[0] = '\0';
        configName[0] = '\0';
        for (int i = 0; i < MAX_STAGES; i++) {
            stages[i] = FermentationStage();
        }
    }
    
    void clear() {
        active = false;
        concluidaMantendoTemp = false;
        memset(activeId, 0, sizeof(activeId));
        memset(configName, 0, sizeof(configName));
        tempTarget = DEFAULT_TEMPERATURE;
        currentStageIndex = 0;
        targetReachedSent = false;
        totalStages = 0;
        lastUpdate = millis();
        stageStartEpoch = 0;
        
        for (int i = 0; i < MAX_STAGES; i++) {
            stages[i] = FermentationStage();
        }
    }
    
    bool hasChanged(const char* newId, bool newActive) const {
        if (active != newActive) return true;
        if (strcmp(activeId, newId) != 0) return true;
        return false;
    }

    void setActiveId(const char* id) {
        if (id) {
            strncpy(activeId, id, sizeof(activeId) - 1);
            activeId[sizeof(activeId) - 1] = '\0';
        } else {
            activeId[0] = '\0';
        }
    }
    
    void setConfigName(const char* name) {
        if (name) {
            strncpy(configName, name, sizeof(configName) - 1);
            configName[sizeof(configName) - 1] = '\0';
        } else {
            configName[0] = '\0';
        }
    }
};

// Declarações externas
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern Rele cooler;
extern Rele heater;
