#include "wifi_manager.h"
#include "secrets.h"

bool setupWiFi(bool verbose) {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    if (verbose) {
        Serial.print("📡 Conectando ao WiFi");
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    const unsigned long timeout = 10000; // 10s

    while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) {
        delay(500);
        if (verbose) Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (verbose) {
            Serial.println("\n✅ WiFi conectado");
            Serial.println(WiFi.localIP());
        }
        return true;
    } else {
        if (verbose) {
            Serial.println("\n⚠️ Falha no WiFi — modo offline");
        }
        WiFi.disconnect();
        return false;
    }
}
