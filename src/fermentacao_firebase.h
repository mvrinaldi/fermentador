#pragma once

#include <Arduino.h>

// Declaração das funções públicas
void setupActiveListener();
void keepListenerAlive();
void stopActiveListener();
void getTargetFermentacao();
void loadConfigParameters(const String& configId);
