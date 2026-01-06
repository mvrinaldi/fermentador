// fermentacao_mysql.h
#pragma once

#include <Arduino.h>
#include "estruturas.h"

// Constantes (apenas se não definidas em definitions.h)
#ifndef ACTIVE_CHECK_INTERVAL
#define ACTIVE_CHECK_INTERVAL 30000UL  // 30 segundos
#endif

#ifndef TEMPERATURE_TOLERANCE
#define TEMPERATURE_TOLERANCE 0.5f
#endif

#ifndef PHASE_CHECK_INTERVAL
#define PHASE_CHECK_INTERVAL 10000UL   // 10 segundos
#endif

// Variáveis globais
extern unsigned long lastActiveCheck;
extern char lastActiveId[64];
extern bool isFirstCheck;
extern unsigned long stageStartTime;
extern bool stageStarted;
extern bool targetReachedTime; // Marca quando atingiu temperatura alvo

// Funções EEPROM
void saveStateToEEPROM();
void loadStateFromEEPROM();
void clearEEPROM();

// Controle de estado
void updateTargetTemperature(float temp);
void deactivateCurrentFermentation();
void setupActiveListener();

// HTTP – Fermentação ativa
void getTargetFermentacao();
void checkPauseOrComplete(); // Verifica comandos do site

// Configuração de etapas
void loadConfigParameters(const char* configId);

// Troca de fase
void verificarTrocaDeFase();
void verificarTargetAtingido();

// Leituras
void enviarLeituraAtual();