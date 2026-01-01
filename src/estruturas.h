#pragma once
#include <Arduino.h>

struct rele {
  uint8_t pino;
  bool estado;
  bool invertido;
  char nome[16];
  
  void atualizar() {
    digitalWrite(pino, invertido ? !estado : estado);
  }
};

struct SystemState {
  float currentTemp;
  float currentHumidity;
  float targetTemp;
  float hysteresis;
  unsigned long lastUpdate;
  char mode[8];
  char configName[20];
  bool active;        // ← fermentação ativa ou não
  bool relayState;
};

struct LocalConfig {
  char wifiSSID[32];
  char wifiPass[64];
  float targetTemp;
  float hysteresis;
  char fbApiKey[64];
  bool useFirebase;
};

struct SensorInfo {
  char nome[20];
  char endereco[17];
};

struct FermentationStage {
  int id;
  char type[14];           // "temperature", "ramp", "gravity"
  float targetTemp;
  float startTemp;       // para rampas
  int durationDays;      // em dias
  int rampTimeHours;     // para rampas
  unsigned long startTime;
  bool completed;
};

struct FermentationConfig {
  char id[25];
  char name[64];
  int currentStageIndex;
  FermentationStage stages[10];  // Máximo 10 etapas
  int stageCount;
  unsigned long startedAt;
  bool active;
};

struct FermentacaoState {
    bool active = false;
    char configName[50] = "";
    float tempTarget = 20.0;
    String activeId = "";
    unsigned long lastUpdate = 0;
    
    void clear() {
        active = false;
        configName[0] = '\0';
        tempTarget = 20.0;
        activeId = "";
        lastUpdate = millis();
    }
    
    bool hasChanged(const String& newId, bool newActive) const {
        return (active != newActive) || (activeId != newId);
    }
};

// Declaração externa (definida em globais.cpp)
extern FermentacaoState fermentacaoState;