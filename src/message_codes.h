//message_codes.h
#pragma once
// Códigos de mensagens compactadas
#define MSG_FCONC    "fconc"  // Fermentação concluída automaticamente - mantendo temperatura
#define MSG_TC       "tc"     // Time Completed (fermentação concluída - para timeRemaining)
#define MSG_CHOLD    "chold"  // completed_holding_temp
#define MSG_FPAUS    "fpaus"  // Fermentação pausada
#define MSG_TARG     "targ"   // Temperatura alvo atingida
#define MSG_STRT     "strt"   // Etapa iniciada
#define MSG_RAMP     "ramp"   // Em rampa
#define MSG_WAIT     "wait"   // Aguardando alvo
#define MSG_RUN      "run"    // Executando
#define MSG_COOL     "cool"   // Resfriando
#define MSG_HEAT     "heat"   // Aquecendo
#define MSG_IDLE     "idle"   // Ocioso
#define MSG_PEAK     "peak"   // Detectando pico
#define MSG_ERR      "err"    // Erro
#define MSG_OFF      "off"    // Desligado

// Códigos de status de etapa
#define ST_TEMP      "t"      // STAGE_TEMPERATURE
#define ST_RAMP      "r"      // STAGE_RAMP
#define ST_GRAV      "g"      // STAGE_GRAVITY
#define ST_GRAVT     "gt"     // STAGE_GRAVITY_TIME

// Códigos de unidade de tempo
#define UNIT_H       "h"      // horas
#define UNIT_D       "d"      // dias
#define UNIT_M       "m"      // minutos

// Códigos para waiting_gravity
#define WG           "wg"     // waiting_gravity

