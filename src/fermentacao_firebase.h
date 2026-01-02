#pragma once

// EEPROM
void saveStateToEEPROM();
void loadStateFromEEPROM();
void clearEEPROM();

// Controle geral
void updateTargetTemperature(float temp);
void deactivateCurrentFermentation();

// Firebase / Listener
void setupActiveListener();
void keepListenerAlive();
void getTargetFermentacao();

// Configuração
void loadConfigParameters(const char* configId);

// Etapas
void verificarTrocaDeFase();
void updateStageIndexInFirebase(int newIndex);

// Leituras
void enviarLeituraAtual();

void verificarTargetAtingido();