// rampa_suave.h
#pragma once

#include <Arduino.h>

// Estrutura para controle de rampa suave
struct SmoothRampState {
    bool active = false;
    float startTemp = 0.0f;
    float endTemp = 0.0f;
    unsigned long startTime = 0;
};

// Declaração das funções de rampa
void setupSmoothRamp(float startTemp, float endTemp);
void updateSmoothRamp();
bool isSmoothRampActive();
float getCurrentRampTarget();