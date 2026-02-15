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
void checkSerialCommands();

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

void checkSerialCommands() {
    #if DEBUG_EEPROM || defined(ENABLE_SERIAL_COMMANDS)
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd.equalsIgnoreCase("CLEAR_EEPROM") || cmd.equalsIgnoreCase("CLEAR_PREFS")) {
            clearAllPreferencesUtil();
        } else if (cmd.equalsIgnoreCase("DIAG_EEPROM") || cmd.equalsIgnoreCase("DIAG_PREFS")) {
            diagnosticPreferences();
        } else if (cmd.equalsIgnoreCase("RESTART")) {
            #if DEBUG_EEPROM
            LOG_EEPROM("Reiniciando...");
            #endif
            delay(1000);
            ESP.restart();
        }
        #if DEBUG_EEPROM
        else if (cmd.equalsIgnoreCase("HELP")) {
            Serial.println(F("\n📋 COMANDOS DISPONÍVEIS:"));
            Serial.println(F("  CLEAR_PREFS  - Limpa todos os Preferences"));
            Serial.println(F("  DIAG_PREFS   - Mostra diagnóstico"));
            Serial.println(F("  RESTART      - Reinicia o ESP"));
            Serial.println(F("  HELP         - Mostra esta ajuda\n"));
        }
        #endif
    }
    #endif
}