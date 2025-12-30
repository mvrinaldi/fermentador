#include "globais.h"
#include <OneWire.h>
#include <DallasTemperature.h>

SystemState state;
LocalConfig config;

// Inicialização dos objetos rele
rele cooler = {PINO_COOLER, false, true, "COOLER"};
rele heater = {PINO_HEATER, false, true, "HEATER"};

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

bool useFirebase = false;