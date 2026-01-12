#include "globais.h"
#include <OneWire.h>
#include <DallasTemperature.h>

LocalConfig config;
FermentacaoState fermentacaoState;
SystemState state;

// Inicialização dos objetos rele
Rele cooler = {PINO_COOLER, false, false, "COOLER"};
Rele heater = {PINO_HEATER, false, false, "HEATER"};

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
