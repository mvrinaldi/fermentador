#pragma once

#include <Arduino.h>
#include <EEPROM.h>

// ===============================================
// LAYOUT UNIFICADO DA EEPROM - 512 BYTES
// ===============================================

#define EEPROM_SIZE 512

// -----------------------------------------------
// SEÇÃO 1: SENSORES (0-63) - 64 bytes
// -----------------------------------------------
#define SENSOR_ADDR_SIZE 17  // 16 chars + '\0'

#define ADDR_SENSOR_FERMENTADOR  0   // Bytes 0-16
#define ADDR_SENSOR_GELADEIRA    32  // Bytes 32-48
// Reservado: 49-63 para futuros sensores

// -----------------------------------------------
// SEÇÃO 2: FERMENTAÇÃO (64-127) - 64 bytes
// -----------------------------------------------
#define ADDR_FERMENTATION_START  64

#define ADDR_ACTIVE_ID           64   // Bytes 64-95   (32 bytes)
#define ADDR_STAGE_INDEX         96   // Bytes 96-99   (4 bytes - int)
#define ADDR_STAGE_START_TIME    100  // Bytes 100-107 (8 bytes - unsigned long long)
#define ADDR_STAGE_STARTED_FLAG  108  // Bytes 108     (1 byte - bool)
#define ADDR_CONFIG_SAVED        109  // Bytes 109     (1 byte - flag de validação)
// Reservado: 110-127 para expansão

// -----------------------------------------------
// SEÇÃO 3: CONFIGURAÇÕES GERAIS (128-191) - 64 bytes
// -----------------------------------------------
#define ADDR_GENERAL_CONFIG_START 128
// Reservado para WiFi, modo operação, etc.

// -----------------------------------------------
// SEÇÃO 4: LIVRE (192-511) - 320 bytes
// -----------------------------------------------
// Disponível para futuras funcionalidades

// ===============================================
// MAPA VISUAL DA EEPROM
// ===============================================
/*
┌─────────────────────────────────────────────┐
│ BYTE 0-63: SENSORES (64 bytes)              │
├─────────────────────────────────────────────┤
│   0-16:  Sensor Fermentador                 │
│   32-48: Sensor Geladeira                   │
│   49-63: Reservado                          │
├─────────────────────────────────────────────┤
│ BYTE 64-127: FERMENTAÇÃO (64 bytes)         │
├─────────────────────────────────────────────┤
│   64-95:   ID Ativo (32 bytes)              │
│   96-99:   Índice Etapa (int)               │
│   100-107: Timestamp Início (unsigned long) │
│   108:     Flag Etapa Iniciada (bool)       │
│   109:     Flag Config Salva (validação)    │
│   110-127: Reservado                        │
├─────────────────────────────────────────────┤
│ BYTE 128-191: CONFIG GERAL (64 bytes)       │
├─────────────────────────────────────────────┤
│   Reservado para futuras configs            │
├─────────────────────────────────────────────┤
│ BYTE 192-511: LIVRE (320 bytes)             │
└─────────────────────────────────────────────┘
*/

// ===============================================
// FUNÇÕES DE DIAGNÓSTICO
// ===============================================

inline void printEEPROMLayout() {
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║        LAYOUT DA EEPROM (512 bytes)       ║"));
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 1: SENSORES (0-63)                  ║"));
    Serial.printf( "║   Sensor Fermentador: %3d-%3d           ║\n", 
                   ADDR_SENSOR_FERMENTADOR, ADDR_SENSOR_FERMENTADOR + 16);
    Serial.printf( "║   Sensor Geladeira:   %3d-%3d           ║\n", 
                   ADDR_SENSOR_GELADEIRA, ADDR_SENSOR_GELADEIRA + 16);
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 2: FERMENTAÇÃO (64-127)             ║"));
    Serial.printf( "║   ID Ativo:           %3d-%3d           ║\n", 
                   ADDR_ACTIVE_ID, ADDR_ACTIVE_ID + 31);
    Serial.printf( "║   Índice Etapa:       %3d-%3d           ║\n", 
                   ADDR_STAGE_INDEX, ADDR_STAGE_INDEX + 3);
    Serial.printf( "║   Timestamp:          %3d-%3d           ║\n", 
                   ADDR_STAGE_START_TIME, ADDR_STAGE_START_TIME + 7);
    Serial.printf( "║   Flags:              %3d-%3d           ║\n", 
                   ADDR_STAGE_STARTED_FLAG, ADDR_CONFIG_SAVED);
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 3: CONFIG GERAL (128-191)           ║"));
    Serial.println(F("║   Reservado                               ║"));
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 4: LIVRE (192-511)                  ║"));
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
}

inline void debugEEPROMContents() {
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║          CONTEÚDO DA EEPROM               ║"));
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    
    // Sensores
    char sensorFerm[SENSOR_ADDR_SIZE];
    char sensorGel[SENSOR_ADDR_SIZE];
    EEPROM.get(ADDR_SENSOR_FERMENTADOR, sensorFerm);
    EEPROM.get(ADDR_SENSOR_GELADEIRA, sensorGel);
    
    Serial.println(F("║ SENSORES:                                 ║"));
    Serial.printf( "║   Fermentador: %-26s ║\n", 
                   sensorFerm[0] ? sensorFerm : "não configurado");
    Serial.printf( "║   Geladeira:   %-26s ║\n", 
                   sensorGel[0] ? sensorGel : "não configurado");
    
    // Fermentação
    char activeId[32];
    int stageIndex;
    unsigned long long stageStartTime;
    bool stageStarted;
    bool configSaved;
    
    EEPROM.get(ADDR_ACTIVE_ID, activeId);
    EEPROM.get(ADDR_STAGE_INDEX, stageIndex);
    EEPROM.get(ADDR_STAGE_START_TIME, stageStartTime);
    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);
    EEPROM.get(ADDR_CONFIG_SAVED, configSaved);
    
    Serial.println(F("╠═══════════════════════════════════════════╣"));
    Serial.println(F("║ FERMENTAÇÃO:                              ║"));
    Serial.printf( "║   Config Válida: %-24s ║\n", 
                   configSaved ? "SIM" : "NÃO");
    
    if (configSaved) {
        Serial.printf( "║   ID Ativo:      %-24s ║\n", 
                       activeId[0] ? activeId : "vazio");
        Serial.printf( "║   Etapa:         %-24d ║\n", stageIndex);
        Serial.printf( "║   Iniciada:      %-24s ║\n", 
                       stageStarted ? "SIM" : "NÃO");
        Serial.printf( "║   Timestamp:     %-24llu ║\n", stageStartTime);
    }
    
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
}

inline void clearEEPROMSection(int startAddr, int endAddr) {
    Serial.printf("🧹 Limpando EEPROM: bytes %d-%d\n", startAddr, endAddr);
    
    for (int i = startAddr; i <= endAddr; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        Serial.println("✅ Seção limpa com sucesso");
    } else {
        Serial.println("❌ Erro ao limpar seção");
    }
}

// Limpa todas as seções
inline void clearAllEEPROM() {
    Serial.println("🧹 Limpando EEPROM completa...");
    
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        Serial.println("✅ EEPROM completamente limpa");
    } else {
        Serial.println("❌ Erro ao limpar EEPROM");
    }
}