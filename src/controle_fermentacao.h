// controle_fermentacao.h
#pragma once

#include <Arduino.h>
#include "estruturas.h"
#include "definitions.h"  // Importa todas as definições

// Variáveis globais
extern unsigned long lastActiveCheck;
extern char lastActiveId[64];
extern bool isFirstCheck;
extern unsigned long stageStartTime;
extern bool stageStarted;

// FUNÇÕES DE TEMPO
time_t getCurrentEpoch();
String formatTime(time_t timestamp);

// FUNÇÃO VALIDAÇÃO
bool isValidString(const char* str);

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
void checkPauseOrComplete();

// Configuração de etapas
void loadConfigParameters(const char* configId);

// Troca de fase
void verificarTrocaDeFase();
void verificarTargetAtingido();

// FUNÇÕES DE ENVIO DE DADOS
void enviarEstadoCompleto();
void enviarLeiturasSensores();
bool readConfiguredTemperatures(float &tempFermenter, float &tempFridge); // Declaração da função (já implementada em gerenciador_sensores.cpp)