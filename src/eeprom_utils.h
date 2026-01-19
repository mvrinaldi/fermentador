// eeprom_utils.h
#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "debug_config.h"

// ==================== FUNÇÕES PÚBLICAS (sempre disponíveis) ====================
#ifdef __cplusplus
extern "C" {
#endif

void clearAllEEPROM();
void diagnosticEEPROM();
void checkSerialCommands();

#ifdef __cplusplus
}
#endif

// ==================== IMPLEMENTAÇÃO ====================
// REMOVER O extern "C" DAQUI - as implementações já correspondem às declarações acima

void clearAllEEPROM() {
    #if DEBUG_EEPROM
    LOG_EEPROM("⚠️ LIMPANDO TODA A EEPROM...");
    #endif
    
    EEPROM.begin(512);
    
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        #if DEBUG_EEPROM
        LOG_EEPROM("✅ EEPROM limpa! Reiniciando em 3s...");
        #endif
        delay(3000);
        ESP.restart();  // ✅ SEMPRE REINICIA
    }
    #if DEBUG_EEPROM
    else {
        LOG_EEPROM("❌ Falha ao limpar EEPROM");
    }
    #endif
}

void diagnosticEEPROM() {
    #if DEBUG_EEPROM
    // Toda a função dentro do #if
    LOG_EEPROM("📊 DIAGNÓSTICO DA EEPROM");
    LOG_EEPROM("==============================================");
    
    EEPROM.begin(512);
    
    int nonZeroCount = 0;
    for (int i = 0; i < 512; i++) {
        if (EEPROM.read(i) != 0) nonZeroCount++;
    }
    
    LOG_EEPROM("Bytes não-zero: %d/512", nonZeroCount);
    LOG_EEPROM("Primeiros 128 bytes:");
    
    for (int i = 0; i < 128; i += 16) {
        char line[64] = {0};
        char* ptr = line;
        ptr += sprintf(ptr, "%03d: ", i);
        
        for (int j = 0; j < 16 && (i + j) < 128; j++) {
            ptr += sprintf(ptr, "%02X ", EEPROM.read(i + j));
        }
        LOG_EEPROM("%s", line);
    }
    
    LOG_EEPROM("==============================================");
    #endif
}

void checkSerialCommands() {
    #if DEBUG_EEPROM || defined(ENABLE_SERIAL_COMMANDS)
    // Só verifica comandos se debug habilitado ou comandos explicitamente habilitados
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd.equalsIgnoreCase("CLEAR_EEPROM")) {
            clearAllEEPROM();
        } else if (cmd.equalsIgnoreCase("DIAG_EEPROM")) {
            diagnosticEEPROM();
        } else if (cmd.equalsIgnoreCase("RESTART")) {
            #if DEBUG_EEPROM
            LOG_EEPROM("🔄 Reiniciando...");
            #endif
            delay(1000);
            ESP.restart();
        }
        #if DEBUG_EEPROM
        else if (cmd.equalsIgnoreCase("HELP")) {
            Serial.println(F("\n📋 COMANDOS DISPONÍVEIS:"));
            Serial.println(F("  CLEAR_EEPROM - Limpa toda a EEPROM"));
            Serial.println(F("  DIAG_EEPROM  - Mostra diagnóstico"));
            Serial.println(F("  RESTART      - Reinicia o ESP"));
            Serial.println(F("  HELP         - Mostra esta ajuda\n"));
        }
        #endif
    }
    #endif
}