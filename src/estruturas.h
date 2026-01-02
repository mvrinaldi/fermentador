#pragma once
#include <Arduino.h>

#define MAX_STAGES 10

// === Estrutura de Relé === //
struct rele {
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
  float currentHumidity;
  float targetTemp;
  float hysteresis;
  unsigned long lastUpdate;
  char mode[8];
  char configName[20];
  bool active;
  bool relayState;
};

// === Configuração Local === //
struct LocalConfig {
  char wifiSSID[32];
  char wifiPass[64];
  float targetTemp;
  float hysteresis;
  char fbApiKey[64];
  bool useFirebase;
};

// === Informação de Sensor === //
struct SensorInfo {
  char nome[20];
  char endereco[17];
};

// === Tipos de Etapa === //
enum StageType {
    STAGE_TEMPERATURE,    // Por tempo
    STAGE_RAMP,          // Rampa gradual
    STAGE_GRAVITY,       // Por gravidade
    STAGE_GRAVITY_TIME   // Por gravidade com timeout
};

// === Etapa de Fermentação === //
struct FermentationStage {
  StageType type;
  float targetTemp;
  float startTemp;         // Temperatura inicial (para rampas)
  int durationDays;
  int rampTimeHours;       // Duração da rampa em horas
  float targetGravity;
  int timeoutDays;
  unsigned long startTime;
  bool completed;
  
  FermentationStage() : 
      type(STAGE_TEMPERATURE), 
      targetTemp(20.0),
      startTemp(0.0),
      durationDays(0),
      rampTimeHours(0),
      targetGravity(0.0),
      timeoutDays(0) {}
};

// === Estado da Fermentação (ESTRUTURA PRINCIPAL) === //
struct FermentacaoState {
    bool active;
    char activeId[64];
    char configName[64];
    float tempTarget;
    int currentStageIndex;
    int totalStages;
    FermentationStage stages[MAX_STAGES];
    unsigned long lastUpdate;
    
    FermentacaoState() : 
        active(false),
        activeId(""),
        tempTarget(20.0),
        currentStageIndex(0),
        totalStages(0),
        lastUpdate(0) {
        configName[0] = '\0';
    }
    
    void clear() {
        active = false;
        activeId[0] = '\0';
        configName[0] = '\0';
        tempTarget = 20.0;
        currentStageIndex = 0;
        totalStages = 0;
        lastUpdate = millis();
        
        // Limpa todas as etapas
        for (int i = 0; i < MAX_STAGES; i++) {
            stages[i] = FermentationStage();
        }
    }
    
    bool hasChanged(const char* newId, bool newActive) const {
        return (active != newActive) || strcmp(activeId, newId) != 0;
    }

};

// Declarações externas (definidas em globais.cpp)
extern FermentacaoState fermentacaoState;
extern SystemState state;
extern rele cooler;
extern rele heater;