#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

// === Bibliotecas padrão === //
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// === Bibliotecas escritas === //
#include "secrets.h"
#include "globais.h"
#include "gerenciador_sensores.h"
#include "firebase_conexao.h"
#include "ispindel_struct.h"
#include "ispindel_handler.h"
#include "ispindel_envio.h"
#include "fermentacao_firebase.h"
#include "controle_temperatura.h"
#include "ota.h"
#include "wifi_manager.h"
#include "network_manager.h"

ESP8266WebServer server(80);

extern RealtimeDatabase Database;

// === Variáveis de Controle de Tempo === //
unsigned long lastTemperatureControl = 0;
unsigned long lastPhaseCheck = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n🚀 Iniciando Fermentador Inteligente AUTÔNOMO");
    Serial.println("================================================");

    // EEPROM
    EEPROM.begin(512);
    Serial.println("✅ EEPROM inicializada (512 bytes)");

    // Relés
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();

    Serial.println("✅ Relés inicializados");
    Serial.printf("   • Cooler: Pino %d (%s)\n",
                  cooler.pino, cooler.invertido ? "invertido" : "normal");
    Serial.printf("   • Heater: Pino %d (%s)\n",
                  heater.pino, heater.invertido ? "invertido" : "normal");

    // Sensores
    setupSensorManager();
    Serial.println("✅ Sensores inicializados");

    // === Network Manager (WiFi + OTA + Firebase) ===
    networkSetup(server);

    if (isFirebaseOnline()) {
        scanAndSendSensors();
    }

    // WebServer / iSpindel
    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web ativo");

    // Estado salvo (local, não depende de Wi-Fi)
    setupActiveListener();

    // Log inicial
    Serial.println("\n================================================");
    Serial.println("✅ Sistema pronto");
    Serial.printf("Fermentação ativa: %s\n",
                  fermentacaoState.active ? "SIM" : "NÃO");

    if (fermentacaoState.active) {
        Serial.printf("ID: %s\n", fermentacaoState.activeId);
        Serial.printf("Config: %s\n", fermentacaoState.configName);
        Serial.printf("Etapa: %d/%d\n",
                      fermentacaoState.currentStageIndex + 1,
                      fermentacaoState.totalStages);
        Serial.printf("Temp alvo: %.1f°C\n", fermentacaoState.tempTarget);
    }

    Serial.println("================================================");
}

void loop() {
    unsigned long now = millis();

    // === Network Manager ===
    networkLoop();

    // WebServer (OTA + iSpindel)
    server.handleClient();

    // Firebase SÓ quando estiver realmente online
    if (isFirebaseOnline()) {
        app.loop();
        Database.loop();
    }

    if (isFirebaseOnline()) {
        keepListenerAlive();
    }


    // 🔁 Verificação Firebase (somente online)
    static unsigned long lastCheck = 0;
    if (isFirebaseOnline() && config.useFirebase &&
        now - lastCheck >= ACTIVE_CHECK_INTERVAL) {
        lastCheck = now;
        getTargetFermentacao();
    }

    // 🍺 Troca de fase (local)
    if (now - lastPhaseCheck >= PHASE_CHECK_INTERVAL) {
        lastPhaseCheck = now;
        verificarTrocaDeFase();
    }

    // 📡 iSpindel
    static unsigned long lastSpindel = 0;
    if (now - lastSpindel >= 10000) {
        lastSpindel = now;
        processCloudUpdatesiSpindel();
    }

    // 🌡️ Controle de temperatura (core do sistema)
    if (now - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = now;
        controle_temperatura();
        enviarLeituraAtual();
        verificarTargetAtingido();
    }

    yield();

}
