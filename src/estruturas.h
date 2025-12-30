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
