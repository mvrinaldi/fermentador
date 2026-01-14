// ota.cpp - Sistema OTA para ESP8266

#include "ota.h"
#include "ElegantOTA.h"

static bool otaInitialized = false;
static unsigned long ota_progress_millis = 0;

bool otaInProgress = false;

void setupOTA(ESP8266WebServer &server) {
    if (otaInitialized) {
        return;
    }

    // ✅ Inicializa ElegantOTA
    ElegantOTA.begin(&server);
    
    // Callback: Início do upload
    ElegantOTA.onStart([]() {
        otaInProgress = true;
    });

    // Callback: Progresso do upload (opcional - apenas para monitoramento)
    ElegantOTA.onProgress([](size_t current, size_t final) {
        // Atualiza a cada 1 segundo
        if (millis() - ota_progress_millis > 1000) {
            ota_progress_millis = millis();
            // Feedback opcional no Serial (descomente se quiser ver progresso)
            // Serial.printf("[OTA] Progresso: %u%%\r", (unsigned int)((current * 100) / final));
        }
    });

    // Callback: Finalização
    ElegantOTA.onEnd([](bool success) {
        otaInProgress = false;
        
        if (success) {
            // Delay para garantir resposta HTTP
            delay(1000);
            ESP.restart();
        }
    });

    otaInitialized = true;
}

void handleOTA() {
    if (otaInitialized) {
        ElegantOTA.loop();
    }
}

bool isOTAInitialized() {
    return otaInitialized;
}

bool isOTAInProgress() {
    return otaInProgress;
}