// preferences_utils.h - Utilitários Preferences
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "debug_config.h"
#include "preferences_layout.h"

// ==================== FUNÇÕES PÚBLICAS ====================
#ifdef __cplusplus
extern "C" {
#endif

void clearAllPreferencesUtil();
void diagnosticPreferences();

#ifdef __cplusplus
}
#endif

// ==================== IMPLEMENTAÇÃO ====================

void clearAllPreferencesUtil() {
    #if DEBUG_EEPROM
    LOG_EEPROM("LIMPANDO TODOS OS NAMESPACES PREFERENCES...");
    #endif
    
    clearAllPreferences();
    
    #if DEBUG_EEPROM
    LOG_EEPROM("Preferences limpos! Reiniciando em 3s...");
    #endif
    delay(3000);
    ESP.restart();
}

void diagnosticPreferences() {
    #if DEBUG_EEPROM
    LOG_EEPROM("DIAGNÓSTICO DO PREFERENCES");
    LOG_EEPROM("==============================================");
    
    printPreferencesLayout();
    debugPreferencesContents();
    
    LOG_EEPROM("==============================================");
    #endif
}
