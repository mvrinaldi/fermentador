#pragma once

#include <Arduino.h>

struct SpindelData {
    char name[32];
    float temperature;
    float gravity;
    float battery;
    float angle;
    long lastUpdate; // Timestamp da última recepção
    bool newDataAvailable; // Flag para avisar outros módulos
};

// Torna a instância acessível globalmente
extern SpindelData mySpindel;
