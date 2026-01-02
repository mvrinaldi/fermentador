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

    // 1. EEPROM
    EEPROM.begin(512);
    Serial.println("✅ EEPROM inicializada (512 bytes)");

    // 2. Relés
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();

    Serial.println("✅ Relés inicializados");
    Serial.printf("   • Cooler: Pino %d (%s)\n",
                  cooler.pino, cooler.invertido ? "invertido" : "normal");
    Serial.printf("   • Heater: Pino %d (%s)\n",
                  heater.pino, heater.invertido ? "invertido" : "normal");

    // 3. Sensores
    setupSensorManager();
    Serial.println("✅ Sensores inicializados");

    // 4. WiFi
    Serial.print("📡 Conectando ao WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi conectado");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n⚠️ WiFi offline — modo autônomo");
    }

    // 5. Firebase
    Serial.print("🔥 Inicializando Firebase... ");
    setupFirebase();
    Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "OFFLINE");

    // 6. Sensores do Firebase
    if (WiFi.status() == WL_CONNECTED && config.useFirebase) {
        loadSensorsFromFirebase();
    }

    // 7. WebServer / iSpindel
    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web ativo");

    // 8. Estado salvo
    setupActiveListener();

    // 9. Fermentação ativa
    if (WiFi.status() == WL_CONNECTED && config.useFirebase) {
        getTargetFermentacao();
    }

    // 10. Log inicial
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

    server.handleClient();
    app.loop();
    Database.loop();

    verificarComandoUpdateSensores();
    keepListenerAlive();

    // 🔁 Verificação Firebase
    static unsigned long lastCheck = 0;
    if (now - lastCheck >= 30000 && WiFi.status() == WL_CONNECTED && config.useFirebase) {
        lastCheck = now;
        getTargetFermentacao();
    }

    // 🌡️ Controle de temperatura
    if (now - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = now;
        controle_temperatura();
        enviarLeituraAtual();
    }

    // 🍺 Troca de fase
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

    // 📶 WiFi watchdog
    static unsigned long lastWiFi = 0;
    if (now - lastWiFi >= 60000) {
        lastWiFi = now;
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
        }
    }
    // Registro de quando a temperatura alvo é atingida
    if (now - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = now;
        controle_temperatura();
        enviarLeituraAtual();
        
        // Adicione esta linha:
        verificarTargetAtingido(); 
    }
    delay(50);
}
