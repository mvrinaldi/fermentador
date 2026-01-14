// rampa_suave.h
#pragma once
#include <Arduino.h>

// Taxa de rampa em °C por hora
#define RAMP_RATE 0.5f

// Diferença mínima de temperatura para ativar rampa
#define RAMP_THRESHOLD 3.0f

struct SmoothRampState {
    bool active;
    float startTemp;
    float endTemp;
    unsigned long startTime;
    
    SmoothRampState() : active(false), startTemp(0), endTemp(0), startTime(0) {}
};

// Funções principais
void setupSmoothRamp(float startTemp, float endTemp);
void updateSmoothRamp();
bool isSmoothRampActive();
float getCurrentRampTarget();

// ✅ NOVAS FUNÇÕES
void cancelSmoothRamp();
void debugSmoothRamp();