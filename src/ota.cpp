// ota.cpp - Sistema OTA Corrigido para ESP8266

#include "ota.h"
#include "ElegantOTA.h"

static bool otaInitialized = false;
static unsigned long ota_progress_millis = 0;

void setupOTA(ESP8266WebServer &server) {
    if (otaInitialized) {
        Serial.println("⚠️ OTA já inicializado");
        return;
    }

    // ✅ ElegantOTA deve ser inicializado SEMPRE
    // Não depende de fermentação ativa ou qualquer outra condição
    ElegantOTA.begin(&server);
    
    // Callback: Início do upload
    ElegantOTA.onStart([]() {
        Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.println("🟡 OTA INICIADO");
        Serial.println("⚠️  ATENÇÃO: NÃO DESLIGUE O DISPOSITIVO!");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    });

    // Callback: Progresso do upload
    ElegantOTA.onProgress([](size_t current, size_t final) {
        // Atualiza a cada 1 segundo
        if (millis() - ota_progress_millis > 1000) {
            ota_progress_millis = millis();
            unsigned int progress = (current * 100) / final;
            
            Serial.printf("📊 OTA Progress: %u%% (%u/%u bytes)\r", 
                         progress, current, final);
        }
    });

    // Callback: Finalização
    ElegantOTA.onEnd([](bool success) {
        Serial.println();  // Nova linha após o progresso
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        
        if (success) {
            Serial.println("🟢 OTA FINALIZADO COM SUCESSO!");
            Serial.println("✅ Firmware atualizado");
            Serial.println("🔄 Reiniciando em 3 segundos...");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            
            delay(3000);
            ESP.restart();
        } else {
            Serial.println("🔴 OTA FALHOU!");
            Serial.println("❌ Verifique:");
            Serial.println("   • Arquivo .bin está correto?");
            Serial.println("   • Conexão WiFi estável?");
            Serial.println("   • Firmware compatível com ESP8266?");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        }
    });

    otaInitialized = true;
    
    Serial.println("✅ OTA inicializado e pronto");
    Serial.println("📡 Para atualizar firmware:");
    Serial.println("   1. Acesse: http://<IP_DO_ESP>/update");
    Serial.println("   2. Selecione o arquivo .bin");
    Serial.println("   3. Clique em 'Update'");
    Serial.println("   4. Aguarde 100% e reinício automático");
}

void handleOTA() {
    if (otaInitialized) {
        ElegantOTA.loop();
    }
}

bool isOTAInitialized() {
    return otaInitialized;
}