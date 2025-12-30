#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

// === Bibliotecas padrão === //
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "ispindel_handler.h"

// === Bibliotecas escritas === //
#include "secrets.h"
#include "globais.h"
#include "gerenciador_sensores.h" // Adicione o header do gerenciador
#include "firebase_conexao.h"    // Adicione o header da conexão
#include "ispindel_struct.h"
#include "ispindel_handler.h"
#include "ispindel_envio.h"

AsyncWebServer server(80);

// --- INSTÂNCIA GLOBAL ---
// Isso resolve o erro "extern Preferences prefs" nos outros arquivos
Preferences prefs;
SpindelData mySpindel;

void setup() {
    Serial.begin(115200);
    Serial.println("\n🚀 Iniciando Fermentador");

    // 1. Inicializa os Pinos
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();

    // 2. Inicializa o Gerenciador de Sensores (e o sistema de Preferences)
    setupSensorManager(); 

    // 3. Inicializa Conexão WiFi e Firebase
    // Supondo que você tenha uma função setupWiFi() no seu projeto
    // setupWiFi(); 
    
    // 4. Inicializa Firebase
    setupFirebase();

    // Busca configurações salvas no Firebase logo ao ligar
    loadSensorsFromFirebase();

    // Registra as rotas do iSpindel
    setupSpindelRoutes(server);

    setupSpindelRoutes(server);
    server.begin();
    Serial.println("🌐 Servidor Web iniciado na porta 80");
}

void loop() {
    app.loop();
    Database.loop();
    
    verificarComandoUpdateSensores(); // Escuta se você mudou algo no HTML em relação aos sensores
    
    // Aqui viria sua lógica de controle de temperatura

    // Verifica se chegaram dados novos do iSpindel e despacha para a nuvem
    processCloudUpdatesiSpindel();
}