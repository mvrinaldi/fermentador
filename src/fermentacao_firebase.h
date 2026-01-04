#pragma once
#include <Arduino.h>
#include "estruturas.h"

// Constantes
#define ACTIVE_CHECK_INTERVAL 30000UL  // 30 segundos
#define TEMPERATURE_TOLERANCE 0.5f

// Variáveis globais (definidas no .cpp)
extern unsigned long lastActiveCheck;
extern char lastActiveId[64];
extern bool isFirstCheck;
extern bool listenerSetup;
extern unsigned long lastListenerCheck;
extern unsigned long stageStartTime;
extern bool stageStarted;

// Funções EEPROM
void saveStateToEEPROM();
void loadStateFromEEPROM();
void clearEEPROM();

// Controle de estado
void updateTargetTemperature(float temp);
void deactivateCurrentFermentation();
void setupActiveListener();
void keepListenerAlive();

// Firebase - Fermentação ativa
void getTargetFermentacao();

// Configuração de etapas
void loadConfigParameters(const char* configId);

// Troca de fase
void verificarTrocaDeFase();
void verificarTargetAtingido();

// Leituras
void enviarLeituraAtual();