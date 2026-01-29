//ispindel_stryct.h - Definição da estrutura de dados iSpindel
#pragma once

#include <Arduino.h>

struct SpindelData {
    char name[32] = {0};
    float temperature = 0.0f;
    float gravity = 0.0f;
    float battery = 0.0f;
    float angle = 0.0f;
    unsigned long lastUpdate = 0;
    bool newDataAvailable = false;
};

// Torna a instância acessível globalmente
extern SpindelData mySpindel;
