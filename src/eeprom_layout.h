#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <cstring>
#include "debug_config.h"

// ===============================================
// LAYOUT UNIFICADO DA EEPROM - 512 BYTES
// ===============================================
#define EEPROM_SIZE 512

// -----------------------------------------------
// SEÇÃO 1: SENSORES (0-63) - 64 bytes
// -----------------------------------------------
#define SENSOR_ADDR_SIZE 17 // 16 chars + '\0'
#define ADDR_SENSOR_FERMENTADOR 0  // Bytes 0-16
#define ADDR_SENSOR_GELADEIRA   32 // Bytes 32-48
// Reservado: 49-63 para futuros sensores

// -----------------------------------------------
// SEÇÃO 2: FERMENTAÇÃO (64-127) - 64 bytes
// -----------------------------------------------
#define ADDR_FERMENTATION_START 64
#define ADDR_ACTIVE_ID          64  // Bytes 64-95 (32 bytes)
#define ADDR_STAGE_INDEX        96  // Bytes 96-99 (4 bytes - int)
#define ADDR_STAGE_START_TIME   100 // Bytes 100-103 (4 bytes - unsigned long)
// Bytes 104-107: RESERVADO
#define ADDR_STAGE_STARTED_FLAG 108 // Byte 108 (1 byte - bool)
#define ADDR_CONFIG_SAVED       109 // Byte 109 (1 byte - flag de validação)
// Reservado: 110-127 para expansão

// -----------------------------------------------
// SEÇÃO 3: CONFIGURAÇÕES GERAIS (128-191) - 64 bytes
// -----------------------------------------------
#define ADDR_GENERAL_CONFIG_START 128

// -----------------------------------------------
// SEÇÃO 4: LIVRE (192-511) - 320 bytes
// -----------------------------------------------

#define ADDR_LAST_VALID_EPOCH 128
#define ADDR_LAST_VALID_MILLIS 132

// ===============================================
// FUNÇÕES AUXILIARES SEGURAS PARA EEPROM
// ===============================================

// Função segura para ler string da EEPROM
inline void eepromReadString(char* dest, size_t destSize, int address, size_t maxLength) {
    if (!dest || destSize == 0) return;
    
    size_t i;
    for (i = 0; i < destSize - 1 && i < maxLength; i++) {
        char c = EEPROM.read(address + i);
        dest[i] = c;
        if (c == 0) break; // Encontrou terminador nulo
    }
    dest[i] = '\0'; // Garante terminação
}

// Função segura para escrever string na EEPROM
inline void eepromWriteString(const char* src, int address, size_t maxLength) {
    if (!src) return;
    
    size_t len = strnlen(src, maxLength);
    for (size_t i = 0; i < maxLength; i++) {
        EEPROM.write(address + i, i < len ? src[i] : 0);
    }
}

// ===============================================
// FUNÇÕES DE DIAGNÓSTICO
// ===============================================

inline void printEEPROMLayout() {
    #if DEBUG_EEPROM
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║      LAYOUT DA EEPROM CORRIGIDO (V3)      ║"));
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 1: SENSORES (0-63)                  ║"));
    Serial.printf( "║ Sensor Fermentador: %3d-%3d               ║\n", 
                   ADDR_SENSOR_FERMENTADOR, ADDR_SENSOR_FERMENTADOR + 16);
    Serial.printf( "║ Sensor Geladeira:   %3d-%3d               ║\n", 
                   ADDR_SENSOR_GELADEIRA, ADDR_SENSOR_GELADEIRA + 16);
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ SEÇÃO 2: FERMENTAÇÃO (64-127)             ║"));
    Serial.printf( "║ ID Ativo:       %3d-%3d (32b)           ║\n", 
                   ADDR_ACTIVE_ID, ADDR_ACTIVE_ID + 31);
    Serial.printf( "║ Índice Etapa:   %3d-%3d (4b)            ║\n", 
                   ADDR_STAGE_INDEX, ADDR_STAGE_INDEX + 3);
    Serial.printf( "║ Timestamp:      %3d-%3d (4b)            ║\n", 
                   ADDR_STAGE_START_TIME, ADDR_STAGE_START_TIME + 3);
    Serial.printf( "║ Flags:          %3d, %3d                 ║\n", 
                   ADDR_STAGE_STARTED_FLAG, ADDR_CONFIG_SAVED);
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
    #endif
}

inline void debugEEPROMContents() {
    #if DEBUG_EEPROM
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║            CONTEÚDO DA EEPROM             ║"));
    Serial.println(F("╠══════════════════════════════════════════╣"));

    // Sensores
    char sensorFerm[SENSOR_ADDR_SIZE] = {0};
    char sensorGel[SENSOR_ADDR_SIZE] = {0};
    
    eepromReadString(sensorFerm, sizeof(sensorFerm), ADDR_SENSOR_FERMENTADOR, SENSOR_ADDR_SIZE);
    eepromReadString(sensorGel, sizeof(sensorGel), ADDR_SENSOR_GELADEIRA, SENSOR_ADDR_SIZE);

    Serial.println(F("║ SENSORES:                                 ║"));
    Serial.printf( "║ Ferm: %-35s ║\n", sensorFerm[0] ? sensorFerm : "vazio");
    Serial.printf( "║ Gel:  %-35s ║\n", sensorGel[0] ? sensorGel : "vazio");

    // Fermentação
    char activeId[32] = {0};
    int stageIndex = 0;
    unsigned long stageStartTime = 0;
    bool stageStarted = false;
    bool configSaved = false;

    eepromReadString(activeId, sizeof(activeId), ADDR_ACTIVE_ID, sizeof(activeId));
    EEPROM.get(ADDR_STAGE_INDEX, stageIndex);
    EEPROM.get(ADDR_STAGE_START_TIME, stageStartTime);
    EEPROM.get(ADDR_STAGE_STARTED_FLAG, stageStarted);
    configSaved = (EEPROM.read(ADDR_CONFIG_SAVED) == 1);

    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ FERMENTAÇÃO:                              ║"));
    Serial.printf( "║ Config Válida: %-26s ║\n", configSaved ? "SIM" : "NÃO");
    
    if (configSaved) {
        Serial.printf( "║ ID Ativo: %-29s ║\n", activeId[0] ? activeId : "vazio");
        Serial.printf( "║ Etapa:    %-29d ║\n", stageIndex);
        Serial.printf( "║ Iniciada: %-29s ║\n", stageStarted ? "SIM" : "NÃO");
        Serial.printf( "║ Tempo:    %-29lu ║\n", stageStartTime);
    }
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
    #endif
}

inline void clearEEPROMSection(int startAddr, int endAddr) {
    if (startAddr < 0 || endAddr >= EEPROM_SIZE || startAddr > endAddr) return;
    
    for (int i = startAddr; i <= endAddr; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
}

inline void clearAllEEPROM() {
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
}