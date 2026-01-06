#pragma once
#include <Arduino.h>

#define MAX_STAGES 10

// === Estrutura de Relé === //
struct Rele {
  uint8_t pino;
  bool estado;
  bool invertido;
  char nome[16];
  
  void atualizar() {
    digitalWrite(pino, invertido ? !estado : estado);
  }
};

// === Estado Geral do Sistema === //
struct SystemState {
  float currentTemp;
  float targetTemp;
  unsigned long lastUpdate;
  char configName[20];
  bool active;
  
  SystemState() : currentTemp(0.0), targetTemp(20.0), lastUpdate(0), active(false) {
    configName[0] = '\0';
  }
};

// === Configuração Local === //
struct LocalConfig {
  float targetTemp;
  float hysteresis;
  bool useHTTP;  // Mudado de useFirebase
  
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
    STAGE_TEMPERATURE,    // Por tempo (mantém temperatura por X dias)
    STAGE_RAMP,          // Rampa gradual de temperatura
    STAGE_GRAVITY,       // Por gravidade (aguarda iSpindel)
    STAGE_GRAVITY_TIME   // Por gravidade com timeout
};

// === Etapa de Fermentação === //
struct FermentationStage {
  StageType type;
  float targetTemp;
  float startTemp;         // Temperatura inicial (para rampas)
  int durationDays;        // Duração em dias (para STAGE_TEMPERATURE)
  int rampTimeHours;       // Duração da rampa em horas (para STAGE_RAMP)
  float targetGravity;     // Gravidade alvo (para STAGE_GRAVITY*)
  int timeoutDays;         // Timeout em dias (para STAGE_GRAVITY_TIME)
  unsigned long startTime;
  bool completed;
  
  FermentationStage() : 
      type(STAGE_TEMPERATURE), 
      targetTemp(20.0),
      startTemp(0.0),
      durationDays(0),
      rampTimeHours(0),
      targetGravity(0.0),
      timeoutDays(0),
      startTime(0),
      completed(false) {}
};

// === Estado da Fermentação (ESTRUTURA PRINCIPAL) === //
struct FermentacaoState {
    bool active;
    char activeId[32];
    char configName[64];
    float tempTarget;
    int currentStageIndex;
    bool targetReachedSent;
    int totalStages;
    FermentationStage stages[MAX_STAGES];
    unsigned long lastUpdate;
    
    FermentacaoState() : 
        active(false),
        tempTarget(20.0),
        currentStageIndex(0),
        targetReachedSent(false),
        totalStages(0),
        lastUpdate(0) {
        activeId[0] = '\0';
        configName[0] = '\0';
        for (int i = 0; i < MAX_STAGES; i++) {
            stages[i] = FermentationStage();
        }
    }
    
    void clear() {
        active = false;
        memset(activeId, 0, sizeof(activeId));
        memset(configName, 0, sizeof(configName));
        tempTarget = 20.0;
        currentStageIndex = 0;
        targetReachedSent = false;
        totalStages = 0;
        lastUpdate = millis();
        
        // Limpa todas as etapas
        for (int i = 0; i < MAX_STAGES; i++) {
            stages[i] = FermentationStage();
        }
    }
    
    bool hasChanged(const char* newId, bool newActive) const {
        if (active != newActive) return true;
        if (strcmp(activeId, newId) != 0) return true;
        return false;
    }

    // Funções auxiliares para manipulação segura de strings
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

// Declarações externas (definidas em globais.cpp)
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern Rele cooler;
extern Rele heater;