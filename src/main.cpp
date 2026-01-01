#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

// === Bibliotecas padrão === //
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

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

// Variáveis para controle de tempo
unsigned long lastTemperatureControl = 0;
const unsigned long TEMPERATURE_CONTROL_INTERVAL = 5000; // 5 segundos

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🚀 Iniciando Fermentador Inteligente");
    Serial.println("===================================");
    
    // 1. Inicializa os Pinos de Controle
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();
    
    Serial.println("✅ Relés inicializados");
    
    // 2. Inicializa Sensores Locais (DS18B20)
    setupSensorManager();
    
    Serial.println("✅ Gerenciador de sensores inicializado");
    
    // 3. Conecta ao WiFi
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
        Serial.println(WiFi.RSSI());
    } else {
        Serial.println("\n❌ Falha na conexão WiFi");
        Serial.println("⚠️ Modo offline ativado");
    }
    
    // 4. Inicializa Firebase
    Serial.print("🔥 Inicializando Firebase... ");
    setupFirebase();
    
    // Verifica se Firebase está funcionando
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ Firebase conectado (WiFi OK)");
    } else {
        Serial.println("⚠️ Firebase não conectado (WiFi offline)");
    }
    
    // 5. Busca configurações de sensores do Firebase
    Serial.print("📥 Carregando configurações de sensores... ");
    loadSensorsFromFirebase();
    Serial.println("✅ Configurações carregadas");
    
    // 6. Configura rotas do servidor web
    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web iniciado na porta 80");
    
    // 7. Configura monitoramento da fermentação ativa
    Serial.print("🎯 Configurando monitoramento de fermentação... ");
    setupActiveListener();
    
    // Primeira verificação da fermentação ativa
    Serial.println("Buscando fermentação ativa...");
    getTargetFermentacao();
    
    // 8. Log inicial do sistema
    Serial.println("\n===================================");
    Serial.println("✅ Sistema inicializado com sucesso!");
    Serial.println("===================================");
    Serial.println("📊 Status inicial:");
    Serial.printf("   • Fermentação ativa: %s\n", fermentacaoState.active ? "SIM" : "NÃO");
    Serial.printf("   • Nome: %s\n", fermentacaoState.configName);
    Serial.printf("   • Temp. Alvo: %.1f°C\n", fermentacaoState.tempTarget);
    Serial.println("===================================\n");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // 1. Processa requisições web
    server.handleClient();
    
    // 2. Mantém loops do Firebase e outras bibliotecas
    app.loop();
    Database.loop();
    
    // 3. Verifica comandos de atualização de sensores via web
    verificarComandoUpdateSensores();
    
    // 4. Mantém o monitoramento da fermentação ativa
    keepListenerAlive();
    
    // 5. Verificação periódica da fermentação ativa (a cada 30s)
    static unsigned long lastActiveCheck = 0;
    if (currentMillis - lastActiveCheck >= 30000) {
        getTargetFermentacao();
        lastActiveCheck = currentMillis;
        
        // Log periódico do estado
        Serial.printf("\n[%lu] 🔄 Verificação periódica:\n", currentMillis / 1000);
        Serial.printf("   • Fermentação: %s\n", fermentacaoState.active ? "ATIVA" : "INATIVA");
        Serial.printf("   • ID: %s\n", fermentacaoState.activeId.c_str());
        Serial.printf("   • Temp. Alvo: %.1f°C\n", fermentacaoState.tempTarget);
    }
    
    // 6. Controle de temperatura
    if (currentMillis - lastTemperatureControl >= TEMPERATURE_CONTROL_INTERVAL) {
        lastTemperatureControl = currentMillis;
        
        // Agora você chama a função única que gerencia PID e segurança
        controle_temperatura();
    }
    
    // 7. Processa dados do iSpindel para envio à nuvem
    static unsigned long lastiSpindelCheck = 0;
    if (currentMillis - lastiSpindelCheck >= 10000) {
        lastiSpindelCheck = currentMillis;
        processCloudUpdatesiSpindel();
    }
    
    // 8. Monitora conexão WiFi
    static unsigned long lastWiFiCheck = 0;
    if (currentMillis - lastWiFiCheck >= 60000) {
        lastWiFiCheck = currentMillis;
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠️ WiFi desconectado, tentando reconectar...");
            WiFi.reconnect();
        }
    }
    
    delay(50);
}