// main.cpp - Fermentador com MySQL e BrewPi
// ✅ CONSOLIDADO: Envios MySQL centralizados, duplicações removidas

#define FIRMWARE_VERSION "1.3.9"
#define IMPLEMENTACAO "Consolidação: envios MySQL unificados, duplicações removidas"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>

#include <time.h>
#include <TZ.h>

// Configuração NTP
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define NTP_SERVER3 "time.google.com"

// Fuso horário do Brasil (Brasília UTC-3)
#define TZ_STRING "BRST3BRDT,M10.3.0/0,M2.3.0/0"

#include "secrets.h"
#include "globais.h"
#include "gerenciador_sensores.h"
#include "http_client.h"
#include "mysql_sender.h"
#include "ispindel_struct.h"
#include "ispindel_handler.h"
#include "ispindel_envio.h"
#include "controle_fermentacao.h"
#include "BrewPiStructs.h"
#include "BrewPiTicks.h"
#include "TempSensor.h"
#include "BrewPiTempControl.h"
#include "ota.h"
#include "wifi_manager.h"
#include "network_manager.h"
#include "eeprom_utils.h"
#include "http_commands.h"
#include "telnet.h"
#include "debug_config.h"

ESP8266WebServer server(80);

// === Variáveis de Controle de Tempo === //
unsigned long lastTemperatureControl = 0;
unsigned long lastPhaseCheck = 0;

// ==================== TIMERS ====================
unsigned long lastSensorCheck = 0;

const unsigned long SENSOR_CHECK_INTERVAL = 30000;   // 30 segundos

int activeConfigId = 0; // Atualizado quando inicia fermentação

// Formata a data de Jan 18 2026 para 18/01/2026
String getBuildDateFormatted() {
    char month[4];
    int day, year;
    static const char monthNames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    
    if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
        return String("01/01/2000");
    }
    
    month[3] = '\0';
    
    const char* monthPtr = strstr(monthNames, month);
    if (monthPtr == NULL) {
        return String("01/01/2000");
    }
    
    int monthIndex = (int)((monthPtr - monthNames) / 3) + 1;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", day, monthIndex, year);
    
    return String(buffer);
}

// Formata a hora de 15:30:21 para 15:30
String getBuildTimeShort() {
    char timeBuf[6];
    memcpy(timeBuf, __TIME__, 5);
    timeBuf[5] = '\0';
    return String(timeBuf);
}

// ============================================
// FUNÇÃO DE SETUP DO NTP (UTC PURO)
// ============================================

void setupNTP() {
    #if DEBUG_MAIN
    Serial.println(F("[NTP] Configurando sincronização de tempo (UTC)..."));
    #endif

    configTime(0, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

    #if DEBUG_MAIN
    Serial.print(F("[NTP] Aguardando sincronização"));
    #endif

    int timeout = 0;
    time_t now = time(nullptr);

    while (now < 1577836800L && timeout < 100) {
        delay(100);
        #if DEBUG_MAIN
        Serial.print(".");
        #endif
        now = time(nullptr);
        timeout++;
    }
    
    #if DEBUG_MAIN
    Serial.println();
    #endif

    if (now > 1577836800L) {
        #if DEBUG_MAIN
        Serial.println(F("[NTP] ✅ Sincronizado com sucesso! (UTC)"));
        
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S UTC", &timeinfo);
        Serial.printf("[NTP] Data/Hora UTC: %s\n", buffer);
        #endif
    } else {
        #if DEBUG_MAIN
        Serial.println(F("[NTP] ⚠️ Falha na sincronização"));
        #endif
    }
}

// ============================================
// MONITORAMENTO PERIÓDICO DO NTP
// ============================================

void checkNTPSync() {
    static unsigned long lastCheck = 0;

    if (millis() - lastCheck > 3600000UL) {
        lastCheck = millis();

        time_t now = time(nullptr);
        if (now < 1577836800L) {
            #if DEBUG_MAIN
            Serial.println(F("[NTP] ⚠️ Perdeu sincronização, tentando reconectar..."));
            #endif
            setupNTP();
        }
    }
}

// ==================== CARREGA CONFIG ATIVA ====================

void loadActiveConfiguration() {
    JsonDocument doc;
    
    if (httpClient.getActiveFermentation(doc)) {
        if (doc["active"] == true) {
            activeConfigId = doc["id"];
            #if DEBUG_MAIN
            Serial.printf("[CONFIG] Fermentação ativa: ID %d\n", activeConfigId);
            #endif
        } else {
            activeConfigId = 0;
        }
    }
}

// ============================================
// SETUP - INTEGRAÇÃO BREWPI
// ============================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    #if DEBUG_MAIN
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════╗");
    Serial.println("║                                                ║");
    Serial.println("║     🚀 FERMENTADOR INTELIGENTE - BREWPI      ║");
    Serial.println("║                                                ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.println("");
    Serial.printf("📦 Firmware: v%s\n", FIRMWARE_VERSION);
    Serial.printf("📅 Compilado: %s às %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("🔐 MD5: %s\n", ESP.getSketchMD5().c_str());
    Serial.printf("💾 Tamanho: %u bytes\n", ESP.getSketchSize());
    Serial.printf("🆓 Espaço OTA: %u bytes\n", ESP.getFreeSketchSpace());
    Serial.println("");
    
    rst_info *resetInfo = ESP.getResetInfoPtr();
    
    Serial.print("🔄 Último reset: ");
    switch (resetInfo->reason) {
        case REASON_DEFAULT_RST:
            Serial.println("Power-on");
            break;
        case REASON_WDT_RST:
            Serial.println("Watchdog Timer");
            break;
        case REASON_EXCEPTION_RST:
            Serial.println("Exception");
            break;
        case REASON_SOFT_WDT_RST:
            Serial.println("Software Watchdog");
            break;
        case REASON_SOFT_RESTART:
            Serial.println("Software Restart");
            break;
        case REASON_DEEP_SLEEP_AWAKE:
            Serial.println("Deep Sleep Wake");
            break;
        case REASON_EXT_SYS_RST:
            Serial.println("Reinício via Software (ESP.restart())");
            Serial.println("💡 POSSÍVEL OTA RECÉM-CONCLUÍDO!");
            break;
        default:
            Serial.printf("Desconhecido (%d)\n", resetInfo->reason);
    }
    
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("");
    #endif

    // ==================== INICIALIZAÇÃO DO SISTEMA ====================
    
    EEPROM.begin(512);
    
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();

    #if DEBUG_SENSORES
    Serial.println("✅ Relés inicializados");
    Serial.printf("   • Cooler: Pino %d (%s)\n",
                  cooler.pino, cooler.invertido ? "invertido" : "normal");
    Serial.printf("   • Heater: Pino %d (%s)\n",
                  heater.pino, heater.invertido ? "invertido" : "normal");
    Serial.println("\n🌡️  Inicializando sensores de temperatura...");
    #endif

    setupSensorManager();
    
    #if DEBUG_MAIN
    Serial.println("✅ Sensores Dallas inicializados");
    Serial.println("\n🍺 Inicializando sistema BrewPi...");
    #endif

    DallasTemperature* dallasPtr = getSensorsPointer();
    
    if (dallasPtr) {
        brewPiControl.setSensors(dallasPtr, 1, 0);
        brewPiControl.setActuators(&cooler, &heater);
        brewPiControl.init();
        
        #if DEBUG_MAIN
        Serial.println("✅ Sistema BrewPi inicializado");
        #endif
    } else {
        #if DEBUG_MAIN
        Serial.println("❌ ERRO: Não foi possível obter ponteiro dos sensores Dallas!");
        #endif
    }
    
    #if DEBUG_MAIN
    Serial.println("\n💾 Carregando estado salvo...");
    #endif

    setupActiveListener();
    
    #if DEBUG_MAIN
    Serial.println("\n📡 Conectando à rede...");
    #endif

    networkSetup(server);
    
    #if DEBUG_TELNET
    telnetSetup();
    Serial.println("📡 Telnet debug ativo");
    #endif

    if (WiFi.status() == WL_CONNECTED) {
        #if DEBUG_MAIN
        Serial.println("\n⏰ Configurando NTP...");
        #endif
        setupNTP();
    }
    
    if (isHTTPOnline()) {
        #if DEBUG_MAIN
        Serial.println(F("\n📡 Enviando sensores detectados ao servidor..."));
        #endif

        scanAndSendSensors();
        
        #if DEBUG_MAIN
        Serial.println(F("\n📥 Buscando configuração de sensores do servidor..."));
        #endif

        String fermenterAddr, fridgeAddr;
        
        if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
            bool updated = false;
            
            if (!fermenterAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR1_NOME, fermenterAddr)) {
                    #if DEBUG_MAIN
                    Serial.println(F("✅ Sensor fermentador salvo na EEPROM"));
                    #endif
                    updated = true;
                }
            }
            
            if (!fridgeAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR2_NOME, fridgeAddr)) {
                    #if DEBUG_MAIN
                    Serial.println(F("✅ Sensor geladeira salvo na EEPROM"));
                    #endif
                    updated = true;
                }
            }
            
            if (updated) {
                #if DEBUG_MAIN
                Serial.println(F("✅ Sensores sincronizados do servidor!"));
                #endif

                setupSensorManager();
                
                DallasTemperature* dallasPtr = getSensorsPointer();
                if (dallasPtr) {
                    brewPiControl.setSensors(dallasPtr, 1, 0);
                    #if DEBUG_MAIN
                    Serial.println(F("✅ Sensores BrewPi atualizados"));
                    #endif
                }
            }
        } else {
            #if DEBUG_MAIN
            Serial.println(F("⚠️ Nenhum sensor configurado no servidor"));
            #endif
        }
        
        if (fermentacaoState.active) {
            #if DEBUG_MAIN
            Serial.println(F("\n🔍 Verificando se fermentação ainda está ativa no servidor..."));
            #endif
            getTargetFermentacao();
        }
    }
    
    #if DEBUG_MAIN
    auto lista = listSensors();
    if (lista.empty()) {
        Serial.println(F("\n⚠️ Nenhum sensor configurado"));
        Serial.println(F("➜ Acesse http://fermentador.mvrinaldi.com.br/sensores.html"));
    } else {
        Serial.printf("\n✅ %d sensor(es) configurado(s):\n", lista.size());
        for (const auto& s : lista) {
            Serial.printf("  - %s: %s\n", s.nome, s.endereco);
        }
    }
    #endif

    setupSpindelRoutes(server);
    
    // Endpoint: /version
    server.on("/version", HTTP_GET, []() {
        String json = "{";
        json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"compiled\":\"" + String(BUILD_DATE) + " " + String(BUILD_TIME) + "\",";
        json += "\"md5\":\"" + ESP.getSketchMD5() + "\",";
        json += "\"size\":" + String(ESP.getSketchSize()) + ",";
        json += "\"free_ota_space\":" + String(ESP.getFreeSketchSpace()) + ",";
        json += "\"control_system\":\"BrewPi\"";
        json += "}";
        
        server.send(200, "application/json", json);
    });
        
    // Endpoint: / - página inicial
    server.on("/", HTTP_GET, []() {
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fermentador BrewPi</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .card {
            background: white;
            border-radius: 20px;
            padding: 40px;
            max-width: 500px;
            width: 100%;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 10px;
            font-size: 28px;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 30px;
        }
        .info-box {
            background: #f8f9fa;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            margin-bottom: 10px;
            font-size: 14px;
        }
        .info-label {
            color: #666;
        }
        .info-value {
            color: #333;
            font-weight: bold;
        }
        .btn {
            display: block;
            width: 100%;
            padding: 15px;
            background: #667eea;
            color: white;
            text-align: center;
            text-decoration: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: bold;
            transition: background 0.3s;
            margin-bottom: 10px;
        }
        .btn:hover {
            background: #5568d3;
        }
        .status {
            display: inline-block;
            padding: 5px 10px;
            background: #10b981;
            color: white;
            border-radius: 5px;
            font-size: 12px;
            font-weight: bold;
        }
        .badge {
            display: inline-block;
            padding: 3px 8px;
            background: #f59e0b;
            color: white;
            border-radius: 3px;
            font-size: 11px;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>🍺 Fermentador BrewPi</h1>
        <p class="subtitle">Sistema de Controle Inteligente <span class="badge">v )" + String(FIRMWARE_VERSION) + R"(</span></p>
        
        <div class="info-box">
            <div class="info-row">
                <span class="info-label">Status:</span>
                <span class="status">✓ ONLINE</span>
            </div>
            <div class="info-row">
                <span class="info-label">Ultima implementação:</span>
                <span class="info-value">)" + String(IMPLEMENTACAO) + R"(</span>
            </div>
            <div class="info-row">
                <span class="info-label">Compilado em:</span>
                <span class="info-value">)" + getBuildDateFormatted() + " às " + getBuildTimeShort() + R"(</span>
            </div>
        </div>
        
        <a href="/update" class="btn">
            🔄 Atualizar Firmware (OTA)
        </a>
    </div>
</body>
</html>
        )";
        
        server.send(200, "text/html", html);
    });
    
    setupOTA(server);
    
    server.begin();
    #if DEBUG_MAIN
    Serial.println("🌐 Servidor Web ativo");
    #endif
    
    if (!fermentacaoState.active) {
        if (state.targetTemp != DEFAULT_TEMPERATURE) {
            #if DEBUG_MAIN
            Serial.printf("[Setup] ⚠️  Ajustando temperatura para padrão: %.1f°C\n", DEFAULT_TEMPERATURE);
            #endif
            updateTargetTemperature(DEFAULT_TEMPERATURE);
        }
    } else {
        if (fermentacaoState.tempTarget < MIN_SAFE_TEMPERATURE || 
            fermentacaoState.tempTarget > MAX_SAFE_TEMPERATURE) {
            #if DEBUG_MAIN
            Serial.printf("[Setup] ⚠️  Temperatura alvo inválida: %.1f°C, ajustando para %.1f°C\n",
                         fermentacaoState.tempTarget, DEFAULT_TEMPERATURE);
            #endif
            updateTargetTemperature(DEFAULT_TEMPERATURE);
        }
    }
   
    #if DEBUG_MAIN
    Serial.println("\n==============================================");
    Serial.println("✅ Sistema BrewPi pronto");
    Serial.printf("Fermentação ativa: %s\n",
                  fermentacaoState.active ? "SIM" : "NÃO");
                  
    if (fermentacaoState.active) {
        Serial.printf("ID: %s\n", fermentacaoState.activeId);
        Serial.printf("Config: %s\n", fermentacaoState.configName);
        Serial.printf("Etapa: %d/%d\n",
                      fermentacaoState.currentStageIndex + 1,
                      fermentacaoState.totalStages);
        Serial.printf("Temp alvo: %.1f°C\n", fermentacaoState.tempTarget);
        
        if (fermentacaoState.stageStartEpoch > 0) {
            time_t now = time(nullptr);
            if (now > 1000000000L) {
                float elapsedH = difftime(now, fermentacaoState.stageStartEpoch) / 3600.0;
                Serial.printf("Tempo decorrido: %.1f horas\n", elapsedH);
            }
        }
        
        Serial.println("[BrewPi] 🔄 Sistema carregado com fermentação ativa");
    } else {
        Serial.printf("Temperatura padrão: %.1f°C\n", DEFAULT_TEMPERATURE);
        Serial.println("[BrewPi] 🛑 Sistema em standby");
    }
    Serial.println("==============================================\n");
    #endif
}

// ============================================
// LOOP - INTEGRAÇÃO BREWPI
// ============================================

void loop() {
    unsigned long now = millis();
    
    // ═══════════════════════════════════════════════════════════════
    // ✅ PRIORIDADE MÁXIMA: OTA EM PROGRESSO
    // ═══════════════════════════════════════════════════════════════
    if (isOTAInProgress()) {
        server.handleClient();
        handleOTA();
        yield();
        return;
    }
    
    // ═══════════════════════════════════════════════════════════════
    // Loop normal (quando OTA NÃO está ativo)
    // ═══════════════════════════════════════════════════════════════
    
    // Comandos seriais
    checkSerialCommands();
    
    // Comandos HTTP
    static unsigned long lastCommandCheck = 0;
    if (now - lastCommandCheck >= 10000) {
        lastCommandCheck = now;
        checkPendingCommands();
    }
    
    // Network Manager
    networkLoop();

    // WebServer
    server.handleClient();
    handleOTA();
    telnetLoop();

    // NTP
    checkNTPSync();
    
    // ═══════════════════════════════════════════════════════════════
    // ✅ CONTROLE DE TEMPERATURA BREWPI (NÚCLEO DO SISTEMA)
    // Executa a cada 5 segundos (conforme BrewPi original)
    // ═══════════════════════════════════════════════════════════════
    if (now - lastTemperatureControl >= 5000) {
        lastTemperatureControl = now;
        
        // Se há fermentação ativa, executa controle BrewPi
        if (fermentacaoState.active) {
            brewPiControl.update();
            
            // Atualiza estado global para compatibilidade
            state.currentTemp = tempToFloat(brewPiControl.getBeerTemp());
            state.targetTemp = fermentacaoState.tempTarget;
        }
        
        // ✅ ENVIO UNIFICADO: Estado completo + controle (30s interno)
        // Substitui: enviarEstadoCompleto + sendControlStateMySQL + parte do heartbeat
        if (isHTTPOnline()) {
            verificarTargetAtingido();
            enviarEstadoCompletoMySQL();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // ✅ HEARTBEAT SIMPLIFICADO (saúde do sistema)
    // Apenas uptime e free_heap - dados de controle já em enviarEstadoCompleto
    // ═══════════════════════════════════════════════════════════════
    if (isHTTPOnline() && activeConfigId > 0) {
        sendHeartbeatMySQL(activeConfigId);  // Controle de tempo interno (30s)
    }

    // ═══════════════════════════════════════════════════════════════
    // VERIFICAÇÃO DE FERMENTAÇÃO ATIVA
    // ═══════════════════════════════════════════════════════════════
    static unsigned long lastCheck = 0;
    if (isHTTPOnline() && now - lastCheck >= ACTIVE_CHECK_INTERVAL) {
        lastCheck = now;
        getTargetFermentacao();
        checkPauseOrComplete();
    }

    // ═══════════════════════════════════════════════════════════════
    // TROCA DE FASE
    // ═══════════════════════════════════════════════════════════════
    if (now - lastPhaseCheck >= PHASE_CHECK_INTERVAL) {
        lastPhaseCheck = now;
        verificarTrocaDeFase();
    }
    
    // ═══════════════════════════════════════════════════════════════
    // ISPINDEL
    // ═══════════════════════════════════════════════════════════════
    static unsigned long lastSpindel = 0;
    if (now - lastSpindel >= 10000) {
        lastSpindel = now;
        processCloudUpdatesiSpindel();
    }
    
    // ═══════════════════════════════════════════════════════════════
    // ✅ ENVIO DE LEITURAS PARA HISTÓRICO (60s)
    // Substitui: enviarLeiturasSensores + sendCurrentTemperaturesMySQL
    // ═══════════════════════════════════════════════════════════════
    if (isHTTPOnline()) {
        enviarLeiturasSensoresMySQL();  // Controle de tempo interno (60s)
    }
    
    // ═══════════════════════════════════════════════════════════════
    // VERIFICAÇÃO PERIÓDICA DE SENSORES
    // ═══════════════════════════════════════════════════════════════
    if (now - lastSensorCheck >= SENSOR_CHECK_INTERVAL) {
        auto lista = listSensors();
        
        if (lista.empty()) {
            if (isHTTPOnline()) {
                scanAndSendSensors();
                
                String fermenterAddr, fridgeAddr;
                if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
                    #if DEBUG_MAIN
                    Serial.println(F("📥 Configuração encontrada no servidor!"));
                    #endif
                    
                    if (!fermenterAddr.isEmpty()) {
                        saveSensorToEEPROM(SENSOR1_NOME, fermenterAddr);
                    }
                    
                    if (!fridgeAddr.isEmpty()) {
                        saveSensorToEEPROM(SENSOR2_NOME, fridgeAddr);
                    }
                    
                    setupSensorManager();
                    DallasTemperature* dallasPtr = getSensorsPointer();
                    if (dallasPtr) {
                        brewPiControl.setSensors(dallasPtr, 1, 0);
                    }
                }
            }
        } else {
            static unsigned long lastSuccessLog = 0;
            if (now - lastSuccessLog >= 300000) {
                #if DEBUG_MAIN
                Serial.printf("✓ %d sensor(es) configurado(s)\n", lista.size());
                #endif
                lastSuccessLog = now;
            }
        }
        
        lastSensorCheck = now;
    }

    yield();
}