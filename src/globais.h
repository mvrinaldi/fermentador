#pragma once
#include <Arduino.h>
#include "estruturas.h"

#define PINO_COOLER 5 // D1 = GPIO5
#define PINO_HEATER 4 // D2 = GPIO4

extern SystemState state;
extern LocalConfig config;
extern rele cooler;
extern rele heater;