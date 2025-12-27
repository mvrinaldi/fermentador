#pragma once
#include <Arduino.h>

struct rele {
  uint8_t pino;
  bool estado;
  bool invertido;
  char nome[10];
  void atualizar() {
    digitalWrite(pino, invertido ? !estado : estado);
  }
};

struct SystemState {
  float currentTemp;
  float currentHumidity;
  float targetTemp;
  float hysteresis;
  bool relayState;
  char mode[8];
  char configName[20];
  unsigned long lastUpdate;
};

struct LocalConfig {
  char wifiSSID[32];
  char wifiPass[64];
  float targetTemp;
  float hysteresis;
  bool useFirebase;
  char fbApiKey[64];
};