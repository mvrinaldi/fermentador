// preferences_layout.h - Layout de chaves para Preferences
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "debug_config.h"

// ===============================================
// NAMESPACES DO PREFERENCES
// ===============================================
#define PREFS_NAMESPACE_SENSORS "sensors"   // Para sensores
#define PREFS_NAMESPACE_FERMENT "ferment"   // Para fermentação

// ===============================================
// CHAVES DO NAMESPACE "sensors" (máx 15 chars)
// ===============================================
#define KEY_SENSOR_FERM "sensorFerm"    // Endereço sensor fermentador
#define KEY_SENSOR_FRIDGE "sensorFridge" // Endereço sensor geladeira

// ===============================================
// CHAVES DO NAMESPACE "ferment" (máx 15 chars)
// ===============================================
#define KEY_ACTIVE_ID "activeId"         // ID da fermentação ativa
#define KEY_STAGE_IDX "stageIdx"         // Índice da etapa atual
#define KEY_STAGE_START "stageStart"     // Timestamp início etapa
#define KEY_STAGE_STARTED "stageStrtd"   // Flag etapa iniciada
#define KEY_CFG_SAVED "cfgSaved"         // Flag config válida
#define KEY_TARGET_REACH "tgtReached"    // Flag temperatura atingida
#define KEY_LAST_EPOCH "lastEpoch"       // Backup NTP epoch
#define KEY_LAST_MILLIS "lastMillis"     // Backup NTP millis

// ===============================================
// FUNÇÕES DE DIAGNÓSTICO
// ===============================================

inline void printPreferencesLayout() {
    #if DEBUG_EEPROM
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║     LAYOUT DO PREFERENCES (MIGRADO)       ║"));
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ NAMESPACE: sensors                        ║"));
    Serial.printf( "║ - sensorFerm:   Endereço fermentador      ║\n");
    Serial.printf( "║ - sensorFridge: Endereço geladeira        ║\n");
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ NAMESPACE: ferment                        ║"));
    Serial.printf( "║ - activeId:     ID fermentação            ║\n");
    Serial.printf( "║ - stageIdx:     Índice etapa              ║\n");
    Serial.printf( "║ - stageStart:   Timestamp início          ║\n");
    Serial.printf( "║ - stageStrtd:   Flag iniciada             ║\n");
    Serial.printf( "║ - cfgSaved:     Flag válida               ║\n");
    Serial.printf( "║ - tgtReached:   Flag temp atingida        ║\n");
    Serial.printf( "║ - lastEpoch:    Backup NTP                ║\n");
    Serial.printf( "║ - lastMillis:   Backup millis             ║\n");
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
    #endif
}

inline void debugPreferencesContents() {
    #if DEBUG_EEPROM
    Serial.println(F("\n╔═══════════════════════════════════════════╗"));
    Serial.println(F("║          CONTEÚDO DO PREFERENCES          ║"));
    Serial.println(F("╠══════════════════════════════════════════╣"));

    // Sensores
    Preferences prefsSensors;
    prefsSensors.begin(PREFS_NAMESPACE_SENSORS, true);
    
    String sensorFerm = prefsSensors.getString(KEY_SENSOR_FERM, "");
    String sensorFridge = prefsSensors.getString(KEY_SENSOR_FRIDGE, "");
    
    prefsSensors.end();

    Serial.println(F("║ SENSORES:                                 ║"));
    Serial.printf( "║ Ferm:  %-34s ║\n", sensorFerm.isEmpty() ? "vazio" : sensorFerm.c_str());
    Serial.printf( "║ Fridge: %-33s ║\n", sensorFridge.isEmpty() ? "vazio" : sensorFridge.c_str());

    // Fermentação
    Preferences prefsFerment;
    prefsFerment.begin(PREFS_NAMESPACE_FERMENT, true);
    
    bool configSaved = prefsFerment.getBool(KEY_CFG_SAVED, false);
    
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.println(F("║ FERMENTAÇÃO:                              ║"));
    Serial.printf( "║ Config Válida: %-26s ║\n", configSaved ? "SIM" : "NÃO");
    
    if (configSaved) {
        String activeId = prefsFerment.getString(KEY_ACTIVE_ID, "");
        int stageIdx = prefsFerment.getInt(KEY_STAGE_IDX, 0);
        bool stageStarted = prefsFerment.getBool(KEY_STAGE_STARTED, false);
        bool targetReached = prefsFerment.getBool(KEY_TARGET_REACH, false);
        unsigned long stageStart = prefsFerment.getULong(KEY_STAGE_START, 0);
        
        Serial.printf( "║ ID Ativo:      %-26s ║\n", activeId.isEmpty() ? "vazio" : activeId.c_str());
        Serial.printf( "║ Etapa:         %-26d ║\n", stageIdx);
        Serial.printf( "║ Iniciada:      %-26s ║\n", stageStarted ? "SIM" : "NÃO");
        Serial.printf( "║ TargetReached: %-26s ║\n", targetReached ? "SIM" : "NÃO");
        Serial.printf( "║ Tempo:         %-26lu ║\n", stageStart);
    }
    
    prefsFerment.end();
    
    Serial.println(F("╚═══════════════════════════════════════════╝\n"));
    #endif
}

inline void clearPreferencesNamespace(const char* namespaceName) {
    Preferences prefs;
    prefs.begin(namespaceName, false);
    prefs.clear();
    prefs.end();
    
    #if DEBUG_EEPROM
    Serial.printf("[Prefs] Namespace '%s' limpo\n", namespaceName);
    #endif
}

inline void clearAllPreferences() {
    clearPreferencesNamespace(PREFS_NAMESPACE_SENSORS);
    clearPreferencesNamespace(PREFS_NAMESPACE_FERMENT);
    
    #if DEBUG_EEPROM
    Serial.println(F("[Prefs] Todos os namespaces limpos"));
    #endif
}