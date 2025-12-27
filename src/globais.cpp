#include "globais.h"

SystemState state;
LocalConfig config;

// Inicialização dos objetos rele
rele cooler = {PINO_COOLER, false, true, "COOLER"};
rele heater = {PINO_HEATER, false, true, "HEATER"};