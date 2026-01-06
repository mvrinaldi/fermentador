// main.cpp - Fermentador com MySQL

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#include "secrets.h"
#include "globais.h"
#include "gerenciador_sensores.h"
#include "http_client.h"
#include "ispindel_struct.h"
#include "ispindel_handler.h"
#include "ispindel_envio.h"
#include "fermentacao_mysql.h"
#include "controle_temperatura.h"
#include "ota.h"
#include "wifi_manager.h"
#include "network_manager.h"

ESP8266WebServer server(80);

// === Variáveis de Controle de Tempo === //
unsigned long lastTemperatureControl = 0;
unsigned long lastPhaseCheck = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🚀 Iniciando Fermentador Inteligente - MySQL");
    Serial.println("==============================================");
    
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
    
    // === Network Manager (WiFi + OTA + HTTP) ===
    networkSetup(server);
    
    if (isHTTPOnline()) {
        scanAndSendSensors();
    }
    
    // WebServer / iSpindel
    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web ativo");
    
    // Estado salvo (local, não depende de Wi-Fi)
    setupActiveListener();
    
    // Log inicial
    Serial.println("\n==============================================");
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
    Serial.println("==============================================");
}

void loop() {
    unsigned long now = millis();
    
    // === Network Manager ===
    networkLoop();
    
    // WebServer (OTA + iSpindel)
    server.handleClient();
    
    // 🔁 Verificação HTTP (somente online)
    static unsigned long lastCheck = 0;
    if (isHTTPOnline() && now - lastCheck >= ACTIVE_CHECK_INTERVAL) {
        lastCheck = now;
        getTargetFermentacao();
        
        // Verifica se foi pausada/concluída pelo site
        checkPauseOrComplete();
    }
    
    // 🍺 Troca de fase (PROCESSAMENTO LOCAL - sempre roda)
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
    
    // 🌡️ Controle de temperatura (CORE DO SISTEMA - sempre roda)
    if (now - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = now;
        controle_temperatura();
        
        // Envia dados ao MySQL apenas se online
        if (isHTTPOnline()) {
            enviarLeituraAtual();
            verificarTargetAtingido();
            
            // Envia estado do controlador
            httpClient.updateControlState(
                fermentacaoState.activeId,
                state.targetTemp,
                cooler.estado,
                heater.estado
            );
        }
    }
    
    yield();
}