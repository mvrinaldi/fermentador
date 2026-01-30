// globais.h
#pragma once
#include <Arduino.h>
#include "estruturas.h"
#include "definitions.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ispindel_struct.h"

// Declaração das variáveis globais (extern)
extern SystemState state;
extern LocalConfig config;
extern FermentacaoState fermentacaoState;
extern Rele cooler;
extern Rele heater;
extern DallasTemperature sensors;

