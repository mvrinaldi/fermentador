// debug_config.h
#pragma once

#include <Arduino.h>
#include "telnet.h"

// ============================================
// CONFIGURAÇÃO DE DEBUG
// ============================================

// CONTROLE GLOBAL
#define DEBUG_MODE 1  // 0=Produção, 1=Desenvolvimento

// FLAGS POR MÓDULO
#define DEBUG_HTTP          0
#define DEBUG_FERMENTATION  0
#define DEBUG_SENSORES      0
#define DEBUG_SENSORES_MAIN 0
#define DEBUG_BREWPI        0
#define DEBUG_EEPROM        0
#define DEBUG_MAIN          0
#define DEBUG_HEARTBEAT     0
#define DEBUG_ENVIODADOS    0
#define DEBUG_ESTADO        0 // Envia logs do State: AGUARDANDO, Cooler: OFF, Heater: OFF ou Wait: Proteção: intervalo mínimo resfriamento
#define DEBUG_ISPINDEL      0
#define DEBUG_TELNET        1 //TELNET SÓ FUNCIONA COM ESSE HABILITADO

// ============================================
// MACROS DE LOG POR MÓDULO
// ============================================

#if DEBUG_HTTP
  #define LOG_HTTP(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_HTTP(x)
#endif

#if DEBUG_FERMENTATION
  #define LOG_FERMENTATION(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_FERMENTATION(x)
#endif

#if DEBUG_SENSORES
  #define LOG_SENSORES(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_SENSORES(x)
#endif

#if DEBUG_SENSORES_MAIN
  #define LOG_SENSORES_MAIN(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_SENSORES_MAIN(x)
#endif

#if DEBUG_BREWPI
  #define LOG_BREWPI(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_BREWPI(x)
#endif

#if DEBUG_EEPROM
  #define LOG_EEPROM(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_EEPROM(x)
#endif

#if DEBUG_MAIN
  #define LOG_MAIN(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_MAIN(x)
#endif

#if DEBUG_HEARTBEAT
  #define LOG_HEARTBEAT(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_HEARTBEAT(x)
#endif

#if DEBUG_ENVIODADOS
  #define LOG_ENVIODADOS(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_ENVIODADOS(x)
#endif

#if DEBUG_ESTADO
  #define LOG_ESTADO(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_ESTADO(x)
#endif

#if DEBUG_ISPINDEL
  #define LOG_ISPINDEL(x) do { Serial.println(x); telnetLog(x); } while(0)
#else
  #define LOG_ISPINDEL(x)
#endif
