#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

// === Bibliotecas padrão === //
#include <Arduino.h>
//#include <ESP8266WiFi.h>
//#include <ESP8266WebServer.h>
//#include <ESPAsyncWebServer.h> // Tente usar essa que é aasíncrona. Senão, use a de cima
//#include <ESP8266mDNS.h>
//#include <LittleFS.h>
//#include <FirebaseClient.h>
//#include "firebase_conexao.h"
//#include <ArduinoJson.h>

// === Bibliotecas escritas === //
#include "secrets.h"
#include "globais.h"
//#include "config.h"
//#include "funcoes.h"
//#include "ota.h"
//#include "watchdog.h"
//#include "sensores.h"
//#include "gerenciadorSensores.h"
//#include "controleTemperatura.h"
//#include "firebase_funcoes.h"


// Se você usa AsyncWebServer, a instância dele costuma ser assim:
//AsyncWebServer server(80);

void setup() {
    Serial.begin(115200);
    Serial.println("\n🚀 Iniciando Fermentador");

    // === Conexões e inicializações === //
  //  inicializarSensorDS18B20();
  //  initFirebaseMutex();
  //  resetarAlertasSistema();

    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
  
    cooler.atualizar();
    heater.atualizar();

//    conectarWiFi();
 //   delay(2000); // Aguarda a rede estabilizar
 //   syncNetworkTime();
 //   setupFirebase();

    // === Inicia o watchdog depois de inicializações pesadas === //
 //   iniciarWatchdog(10);

    // 1. Iniciar Sistema de Arquivos
//    if (!LittleFS.begin()) {
//        Serial.println("❌ Erro ao montar LittleFS");
//        return;
//    }
//    Serial.println("✅ LittleFS montado");

    // Configura servidor OTA
//    setupOTA(server);
//    server.on("/", []() {
//        server.send(200, "text/plain", "Ola! Este e o ElegantOTA.");
//    });
//    setupSensorRoutes();
//    server.begin();
//    Serial.println("Servidor HTTP iniciado");
}

void loop() {
//    static unsigned long lastFirebaseManage = 0;
//    static unsigned long lastConnectionCheck = 0;
//    static unsigned long lastMySQLSend = 0;
    
//    const unsigned long FIREBASE_MANAGE_INTERVAL = 1000;  // 1 segundo
//    const unsigned long CONNECTION_CHECK_INTERVAL = 10000; // 10 segundos
//    unsigned long now = millis();

//    alimentarWatchdog();
    
    // Handle servidor e OTA
//    server.handleClient();
//    ElegantOTA.loop();
//    app.loop();

    // Operações principais
//    gerenciarTemperaturaBarril();
//    gerenciarCarbonatacao(canais, numCanais);

    // Gerenciamento Firebase
//    manageFirebaseOperations();
    delay(10);

    // Verificação de conexões
//    if (now - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL) {
//        lastConnectionCheck = now;
//        checkConnections();

 //       if (!app.ready()) {
 //           tentativasFirebase++;
 //           if (tentativasFirebase <= MAX_TENTATIVAS_FIREBASE) {
 //               Serial.printf("Tentando reconectar Firebase (%d/%d)...\n", 
 //                           tentativasFirebase, MAX_TENTATIVAS_FIREBASE);
 //               setupFirebase();
 //           } else {
 //               Serial.println("Limite de tentativas Firebase atingido - Reiniciando");
 //               ESP.restart();
 //           }
 //       } else {
 //           tentativasFirebase = 0;
 //       }
 //   }
    
    // Delay mínimo para yield
 //   delay(10);
}