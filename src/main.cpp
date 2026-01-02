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
    
    // 1. Inicializa EEPROM
    EEPROM.begin(512);
    Serial.println("✅ EEPROM inicializada (512 bytes)");
    
    // 2. Inicializa os Pinos de Controle
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();
    
    Serial.println("✅ Relés inicializados");
    Serial.printf("   • Cooler: Pino %d (lógica %s)\n", 
                  cooler.pino, cooler.invertido ? "invertida" : "normal");
    Serial.printf("   • Heater: Pino %d (lógica %s)\n", 
                  heater.pino, heater.invertido ? "invertida" : "normal");
    
    // 3. Inicializa Sensores Locais (DS18B20)
    setupSensorManager();
    Serial.println("✅ Gerenciador de sensores inicializado");
    
    // 4. Conecta ao WiFi
    Serial.print("📡 Conectando ao WiFi");
    int wifiAttempts = 0;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi conectado!");
        Serial.print("📶 IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("📶 RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("\n❌ Falha na conexão WiFi");
        Serial.println("⚠️ Modo offline ativado - Sistema funcionará autonomamente");
    }
    
    // 5. Inicializa Firebase
    Serial.print("🔥 Inicializando Firebase... ");
    setupFirebase();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ Firebase conectado");
    } else {
        Serial.println("⚠️ Firebase não conectado (WiFi offline)");
    }
    
    // 6. Busca configurações de sensores do Firebase
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("📥 Carregando configurações de sensores... ");
        loadSensorsFromFirebase();
        Serial.println("✅ Configurações carregadas");
    }
    
    // 7. Configura rotas do servidor web (iSpindel)
    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web iniciado na porta 80");
    Serial.println("   • Endpoint iSpindel: http://" + WiFi.localIP().toString() + "/ispindel");
    
    // 8. Configura monitoramento da fermentação ativa
    Serial.println("🎯 Configurando monitoramento de fermentação...");
    setupActiveListener(); // Carrega estado da EEPROM se existir
    
    // 9. Busca fermentação ativa do Firebase (se WiFi disponível)
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("🔍 Buscando fermentação ativa no Firebase...");
        getTargetFermentacao();
    } else {
        Serial.println("⚠️ Sem WiFi - usando estado recuperado da EEPROM");
    }
    
    // 10. Log inicial do sistema
    Serial.println("\n================================================");
    Serial.println("✅ Sistema inicializado com sucesso!");
    Serial.println("================================================");
    Serial.println("📊 Status inicial:");
    Serial.printf("   • Fermentação ativa: %s\n", fermentacaoState.active ? "SIM" : "NÃO");
    
    if (fermentacaoState.active) {
        Serial.printf("   • ID: %s\n", fermentacaoState.activeId.c_str());
        Serial.printf("   • Nome: %s\n", fermentacaoState.configName);
        Serial.printf("   • Etapa atual: %d/%d\n", 
                      fermentacaoState.currentStageIndex + 1,
                      fermentacaoState.totalStages);
        Serial.printf("   • Temp. Alvo: %.1f°C\n", fermentacaoState.tempTarget);
    }
    
    Serial.println("================================================");
    Serial.println("🔄 Entrando no loop principal...\n");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // 1. Processa requisições web (iSpindel e outros)
    server.handleClient();
    
    // 2. Mantém loops do Firebase e outras bibliotecas
    app.loop();
    Database.loop();
    
    // 3. Verifica comandos de atualização de sensores via web
    verificarComandoUpdateSensores();
    
    // 4. Mantém o monitoramento da fermentação ativa (Firebase)
    keepListenerAlive();
    
    // 5. Verificação periódica da fermentação ativa no Firebase (a cada 30s)
    static unsigned long lastActiveCheck = 0;
    if (WiFi.status() == WL_CONNECTED && 
        currentMillis - lastActiveCheck >= 30000) {
        
        lastActiveCheck = currentMillis;
        getTargetFermentacao();
        
        // Log periódico do estado
        Serial.printf("\n[%lu] 🔄 Verificação periódica Firebase:\n", currentMillis / 1000);
        Serial.printf("   • Fermentação: %s\n", fermentacaoState.active ? "ATIVA" : "INATIVA");
        
        if (fermentacaoState.active) {
            Serial.printf("   • ID: %s\n", fermentacaoState.activeId.c_str());
            Serial.printf("   • Etapa: %d/%d\n", 
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages);
            Serial.printf("   • Temp. Alvo: %.1f°C\n", fermentacaoState.tempTarget);
        }
    }
    
    // 6. Controle de temperatura (a cada 5 segundos)
    if (currentMillis - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = currentMillis;
        controle_temperatura();
    }
    
    // 7. ⭐ VERIFICAÇÃO DE TROCA DE FASE AUTÔNOMA (a cada 10 segundos) ⭐
    if (currentMillis - lastPhaseCheck >= PHASE_CHECK_INTERVAL) {
        lastPhaseCheck = currentMillis;
        verificarTrocaDeFase(); // FUNÇÃO PRINCIPAL DO SISTEMA AUTÔNOMO
    }
    
    // 8. Processa dados do iSpindel para envio à nuvem
    static unsigned long lastiSpindelCheck = 0;
    if (currentMillis - lastiSpindelCheck >= 10000) {
        lastiSpindelCheck = currentMillis;
        processCloudUpdatesiSpindel();
        
        // Debug do iSpindel
        if (mySpindel.lastUpdate > 0 && 
            (currentMillis - mySpindel.lastUpdate < 300000)) { // 5 minutos
            Serial.printf("[iSpindel] Gravidade: %.3f | Temp: %.1f°C | Bateria: %.1fV\n",
                         mySpindel.gravity, 
                         mySpindel.temperature,
                         mySpindel.battery);
        }
    }
    
    // 9. Monitora conexão WiFi e tenta reconectar
    static unsigned long lastWiFiCheck = 0;
    if (currentMillis - lastWiFiCheck >= 60000) {
        lastWiFiCheck = currentMillis;
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠️ WiFi desconectado, tentando reconectar...");
            WiFi.reconnect();
        } else {
            // Log de conectividade
            Serial.printf("📶 WiFi OK | RSSI: %d dBm\n", WiFi.RSSI());
        }
    }
    
    // 10. Debug completo do sistema (a cada 30 segundos)
    static unsigned long lastSystemDebug = 0;
    if (currentMillis - lastSystemDebug >= 30000) {
        lastSystemDebug = currentMillis;
        
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║     STATUS GERAL DO SISTEMA           ║");
        Serial.println("╠════════════════════════════════════════╣");
        
        // Status de conexão
        Serial.printf("║ WiFi: %-31s ║\n", 
                     WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
        Serial.printf("║ Uptime: %-29lu s ║\n", currentMillis / 1000);
        
        // Status da fermentação
        if (fermentacaoState.active) {
            Serial.println("╠════════════════════════════════════════╣");
            Serial.printf("║ Fermentação: %-25s ║\n", fermentacaoState.configName);
            Serial.printf("║ Etapa: %d/%d %-27s║\n", 
                         fermentacaoState.currentStageIndex + 1,
                         fermentacaoState.totalStages, "");
            
            // Tipo da etapa atual
            if (fermentacaoState.currentStageIndex < fermentacaoState.totalStages) {
                FermentationStage& stage = fermentacaoState.stages[fermentacaoState.currentStageIndex];
                const char* typeStr = "";
                switch (stage.type) {
                    case STAGE_TEMPERATURE: typeStr = "Temperatura"; break;
                    case STAGE_RAMP: typeStr = "Rampa"; break;
                    case STAGE_GRAVITY: typeStr = "Gravidade"; break;
                    case STAGE_GRAVITY_TIME: typeStr = "Gravidade+Tempo"; break;
                }
                Serial.printf("║ Tipo: %-31s ║\n", typeStr);
            }
            
            Serial.printf("║ Temp. Atual: %.1f°C %-19s║\n", state.currentTemp, "");
            Serial.printf("║ Temp. Alvo:  %.1f°C %-19s║\n", fermentacaoState.tempTarget, "");
            Serial.printf("║ Cooler: %-30s ║\n", cooler.estado ? "LIGADO" : "DESLIGADO");
            Serial.printf("║ Heater: %-30s ║\n", heater.estado ? "LIGADO" : "DESLIGADO");
            
            // iSpindel
            if (mySpindel.lastUpdate > 0 && 
                (currentMillis - mySpindel.lastUpdate < 300000)) {
                Serial.println("╠════════════════════════════════════════╣");
                Serial.printf("║ iSpindel Gravidade: %.3f %-13s║\n", mySpindel.gravity, "");
                Serial.printf("║ iSpindel Bateria: %.1fV %-15s║\n", mySpindel.battery, "");
            }
        } else {
            Serial.println("╠════════════════════════════════════════╣");
            Serial.println("║ Fermentação: INATIVA                   ║");
        }
        
        Serial.println("╚════════════════════════════════════════╝\n");
    }
    
    delay(50);
}