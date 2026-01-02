#pragma once

#include <Arduino.h>

// Declaração das funções públicas
void setupActiveListener();
void keepListenerAlive();
void stopActiveListener();
void getTargetFermentacao();
void loadConfigParameters(const String& configId);

// NOVA FUNÇÃO PRINCIPAL - Chamada no loop()
void verificarTrocaDeFase();

// Funções de persistência
void saveStateToEEPROM();
void loadStateFromEEPROM();
void clearEEPROM();

// Atualização do Firebase
void updateStageIndexInFirebase(int newIndex);