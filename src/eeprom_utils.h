// eeprom_utils.h - Utilitários para gerenciar EEPROM
#pragma once
#include <Arduino.h>
#include <EEPROM.h>

// ==================== FUNÇÃO PARA LIMPAR TODA A EEPROM ====================
void clearAllEEPROM() {
    Serial.println(F("\n⚠️ LIMPANDO TODA A EEPROM..."));
    
    EEPROM.begin(512);
    
    // Zera todos os 512 bytes
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    
    if (EEPROM.commit()) {
        Serial.println(F("✅ EEPROM completamente limpa!"));
        Serial.println(F("🔄 Reiniciando ESP em 3 segundos..."));
        delay(3000);
        ESP.restart();
    } else {
        Serial.println(F("❌ Falha ao limpar EEPROM"));
    }
}

// ==================== FUNÇÃO PARA DIAGNOSTICAR EEPROM ====================
void diagnosticEEPROM() {
    Serial.println(F("\n📊 DIAGNÓSTICO DA EEPROM"));
    Serial.println(F("=============================================="));
    
    EEPROM.begin(512);
    
    // Verifica bytes não-zero
    int nonZeroCount = 0;
    for (int i = 0; i < 512; i++) {
        if (EEPROM.read(i) != 0) {
            nonZeroCount++;
        }
    }
    
    Serial.printf("Bytes não-zero: %d/512\n", nonZeroCount);
    
    // Mostra primeiros 128 bytes (área de configuração)
    Serial.println(F("\nPrimeiros 128 bytes (config):"));
    for (int i = 0; i < 128; i += 16) {
        Serial.printf("%03d: ", i);
        for (int j = 0; j < 16 && (i + j) < 128; j++) {
            Serial.printf("%02X ", EEPROM.read(i + j));
        }
        Serial.println();
    }
    
    Serial.println(F("==============================================\n"));
}

// ==================== COMANDOS VIA SERIAL ====================
void checkSerialCommands() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd.equalsIgnoreCase("CLEAR_EEPROM")) {
            clearAllEEPROM();
        } else if (cmd.equalsIgnoreCase("DIAG_EEPROM")) {
            diagnosticEEPROM();
        } else if (cmd.equalsIgnoreCase("RESTART")) {
            Serial.println(F("🔄 Reiniciando..."));
            delay(1000);
            ESP.restart();
        } else if (cmd.equalsIgnoreCase("HELP")) {
            Serial.println(F("\n📋 COMANDOS DISPONÍVEIS:"));
            Serial.println(F("  CLEAR_EEPROM - Limpa toda a EEPROM"));
            Serial.println(F("  DIAG_EEPROM  - Mostra diagnóstico"));
            Serial.println(F("  RESTART      - Reinicia o ESP"));
            Serial.println(F("  HELP         - Mostra esta ajuda\n"));
        }
    }
}