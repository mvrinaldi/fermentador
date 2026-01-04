#include "ota.h"
#include "ElegantOTA.h"

static bool otaEnabled = false;
static unsigned long ota_progress_millis = 0;

void setOTAEnabled(bool enabled) {
    otaEnabled = enabled;
}

bool isOTAEnabled() {
    return otaEnabled;
}

void setupOTA(ESP8266WebServer &server) {
    if (!otaEnabled) {
        Serial.println("🚫 OTA desabilitado (fermentação ativa)");
        return;
    }

    ElegantOTA.begin(&server);
    ElegantOTA.onStart([]() {
        Serial.println("🟡 OTA iniciado");
    });

    ElegantOTA.onProgress([](size_t current, size_t final) {
        if (millis() - ota_progress_millis > 1000) {
            ota_progress_millis = millis();
            Serial.printf("OTA Progress: %u%%\n", (current * 100) / final);
        }
    });

    ElegantOTA.onEnd([](bool success) {
        Serial.println(success ? "🟢 OTA finalizado" : "🔴 OTA falhou");
    });

    Serial.println("✅ OTA habilitado");
}
