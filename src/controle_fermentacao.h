// controle_fermentacao.h
// ✅ CONSOLIDADO: Envios MySQL centralizados em mysql_sender.cpp
#pragma once

#include <Arduino.h>
#include "estruturas.h"
#include "definitions.h"

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

// FUNÇÕES DE ENVIO DE DADOS (wrappers para mysql_sender)
void enviarEstadoCompleto();
void enviarLeiturasSensores();

// Declaração da função de leitura de temperatura (implementada em gerenciador_sensores.cpp)
bool readConfiguredTemperatures(float &tempFermenter, float &tempFridge);