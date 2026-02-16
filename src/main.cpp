// main.cpp - Fermentador com MySQL e BrewPi

#define FIRMWARE_VERSION "1.0.4"
#define IMPLEMENTACAO "Corrigido problema reinício incorreto"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Preferences.h>

#include <time.h>
#include <TZ.h>

#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define NTP_SERVER3 "time.google.com"
#define TZ_STRING "BRST3BRDT,M10.3.0/0,M2.3.0/0"

#include "secrets.h"
#include "debug_config.h"
#include "telnet.h"
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
#include "preferences_utils.h"
#include "http_commands.h"

ESP8266WebServer server(80);

unsigned long lastTemperatureControl = 0;
unsigned long lastPhaseCheck = 0;
unsigned long lastSensorCheck = 0;

const unsigned long SENSOR_CHECK_INTERVAL = 30000;

// ========== Funções Auxiliares ==========

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

String getBuildTimeShort() {
    char timeBuf[6];
    memcpy(timeBuf, __TIME__, 5);
    timeBuf[5] = '\0';
    return String(timeBuf);
}

// ========== Setup NTP Melhorado ==========

void setupNTP() {
    LOG_MAIN(F("\n╔════════════════════════════════════╗"));
    LOG_MAIN(F("║     SINCRONIZANDO RELÓGIO NTP      ║"));
    LOG_MAIN(F("╚════════════════════════════════════╝"));
    
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
    
    telnetLog("[NTP] Aguardando sincronização");
    
    int timeout = 0;
    time_t now = time(nullptr);
    
    while (now < 1577836800L && timeout < 100) {
        delay(100);
        now = time(nullptr);
        timeout++;
    }
        
    if (now >= 1577836800L) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        
        LOG_MAIN(F("[NTP] Sincronização bem-sucedida!"));
        
        char buffer[80];
        snprintf(buffer, sizeof(buffer), 
                "[NTP] Data/Hora UTC: %02d/%02d/%04d %02d:%02d:%02d",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        LOG_MAIN(buffer);
        
        snprintf(buffer, sizeof(buffer), "[NTP] Epoch: %lu", (unsigned long)now);
        LOG_MAIN(buffer);
        
        // Salva backup em Preferences
        Preferences prefs;
        prefs.begin("ferment", false);
        prefs.putULong("lastEpoch", now);
        prefs.putULong("lastMillis", millis());
        prefs.end();
        
        LOG_MAIN(F("[NTP] Backup salvo em Preferences"));
        
    } else {
        LOG_MAIN(F("[NTP] FALHA NA SINCRONIZAÇÃO!"));
        LOG_MAIN(F("[NTP] Tentando usar backup..."));
        
        Preferences prefs;
        prefs.begin("ferment", true);
        time_t backupEpoch = prefs.getULong("lastEpoch", 0);
        unsigned long backupMillis = prefs.getULong("lastMillis", 0);
        prefs.end();
        
        if (backupEpoch > 1577836800L) {
            unsigned long elapsed = (millis() - backupMillis) / 1000;
            time_t estimatedNow = backupEpoch + elapsed;
            
            struct tm timeinfo;
            gmtime_r(&estimatedNow, &timeinfo);
            
            LOG_MAIN(F("[NTP] Usando horário estimado do backup:"));
            
            char buffer[80];
            snprintf(buffer, sizeof(buffer),
                    "[NTP] Data/Hora UTC: %02d/%02d/%04d %02d:%02d:%02d",
                    timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            LOG_MAIN(buffer);
            
            snprintf(buffer, sizeof(buffer), "[NTP] Precisão: ±%lu segundos", elapsed);
            LOG_MAIN(buffer);
            
            LOG_MAIN(F("[NTP] ATENÇÃO: Contagem de etapas pode estar imprecisa!"));
        } else {
            LOG_MAIN(F("[NTP] Sem backup válido!"));
            LOG_MAIN(F("[NTP] Sistema funcionará com horário inválido"));
            LOG_MAIN(F("[NTP] Contagem de etapas NÃO funcionará!"));
        }
    }
    
    LOG_MAIN("");
}

// ========== Check NTP Melhorado ==========

void checkNTPSync() {
    static unsigned long lastCheck = 0;
    
    if (millis() - lastCheck < 3600000UL) {
        return;
    }
    
    lastCheck = millis();
    
    time_t now = time(nullptr);
    
    if (now < 1577836800L) {
        LOG_MAIN(F("\n[NTP] Relógio dessincronizado! Tentando ressincronizar..."));
        
        setupNTP();
        
        now = time(nullptr);
        if (now < 1577836800L) {
            LOG_MAIN(F("[NTP] Ressincronização FALHOU!"));
            LOG_MAIN(F("[NTP] Sistema continuará com horário estimado\n"));
        } else {
            LOG_MAIN(F("[NTP] Ressincronização bem-sucedida!\n"));
        }
    }
}

// ========== Comandos Serial ==========

void checkSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd == "TIME" || cmd == "CLOCK" || cmd == "NTP") {
        
        telnetLog("\n╔════════════════════════════════════════╗");
        telnetLog("║      DIAGNÓSTICO DO RELÓGIO NTP        ║");
        telnetLog("╠════════════════════════════════════════╣");
        
        time_t now = time(nullptr);
        
        if (now < 1577836800L) {            
            telnetLog("║ Status:     ❌ DESSINCRONIZADO         ║");
            telnetLog("║ Digite 'SYNC' para forçar NTP          ║");
            telnetLog("╚════════════════════════════════════════╝\n");
        } else {
            struct tm timeinfo;
            gmtime_r(&now, &timeinfo);
            
            time_t brTime = now - (3 * 3600);
            gmtime_r(&brTime, &timeinfo);
            
            telnetLog("║ Status:     ✅ SINCRONIZADO            ║");
            char buffer[80];
            snprintf(buffer, sizeof(buffer), "║ Data: %02d/%02d/%04d  Hora UTC: %02d:%02d:%02d ║",
                    timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            telnetLog(buffer);
            telnetLog("╚════════════════════════════════════════╝\n");
        }
    }
    else if (cmd == "SYNC") {
        telnetLog("\n[Comando] Forçando sincronização NTP...");
        setupNTP();
    }
}

// ========== SETUP ==========

void setup() {
    Serial.begin(115200);
    delay(1000);
        
    pinMode(cooler.pino, OUTPUT);
    pinMode(heater.pino, OUTPUT);
    cooler.atualizar();
    heater.atualizar();

    setupSensorManager();
    
    DallasTemperature* dallasPtr = getSensorsPointer();
    
    if (dallasPtr) {
        brewPiControl.setSensors(dallasPtr, 1, 0);
        brewPiControl.setActuators(&cooler, &heater);
        brewPiControl.init();
    }
    
    setupActiveListener();
    networkSetup(server);
    
    // ✅ Telnet APÓS WiFi conectar
    #if DEBUG_TELNET
        if (WiFi.status() == WL_CONNECTED) {
            telnetSetup();
            LOG_MAIN(F("[Telnet] ✅ Servidor iniciado na porta 23"));
        }
    #endif

    if (WiFi.status() == WL_CONNECTED) {
        setupNTP();
    }
    
    if (isHTTPOnline()) {
        scanAndSendSensors();
        
        String fermenterAddr, fridgeAddr;
        
        if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
            bool updated = false;
            
            if (!fermenterAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR1_NOME, fermenterAddr)) {
                    updated = true;
                }
            }
            
            if (!fridgeAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR2_NOME, fridgeAddr)) {
                    updated = true;
                }
            }
            
            if (updated) {
                setupSensorManager();
                
                DallasTemperature* dallasPtr = getSensorsPointer();
                if (dallasPtr) {
                    brewPiControl.setSensors(dallasPtr, 1, 0);
                }
            }
        }
        
        if (fermentacaoState.active) {
            getTargetFermentacao();
        }
    }
    
    setupSpindelRoutes(server);
    
    server.on("/version", HTTP_GET, []() {
        String json = "{";
        json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"compiled\":\"" + String(BUILD_DATE) + " " + String(BUILD_TIME) + "\",";
        json += "\"md5\":\"" + ESP.getSketchMD5() + "\",";
        json += "\"size\":" + String(ESP.getSketchSize()) + ",";
        json += "\"free_ota_space\":" + String(ESP.getFreeSketchSpace()) + ",";
        json += "\"control_system\":\"BrewPi\",";
        json += "\"storage\":\"Preferences\"";
        json += "}";
        
        server.send(200, "application/json", json);
    });
        
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
                <span class="info-label">Última implementação:</span>
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
    
    if (!fermentacaoState.active) {
        if (state.targetTemp != DEFAULT_TEMPERATURE) {
            updateTargetTemperature(DEFAULT_TEMPERATURE);
        }
    } else {
        if (fermentacaoState.tempTarget < MIN_SAFE_TEMPERATURE || 
            fermentacaoState.tempTarget > MAX_SAFE_TEMPERATURE) {
            updateTargetTemperature(DEFAULT_TEMPERATURE);
        }
    }
    
    LOG_MAIN(F("\n✅ Sistema inicializado com sucesso!\n"));
}

// ========== LOOP ==========

void loop() {
    unsigned long now = millis();
    
    if (isOTAInProgress()) {
        server.handleClient();
        handleOTA();
        yield();
        return;
    }
    
    checkSerialCommands();
    
    static unsigned long lastCommandCheck = 0;
    if (now - lastCommandCheck >= 10000) {
        lastCommandCheck = now;
        checkPendingCommands();
    }
    
    networkLoop();
    server.handleClient();
    handleOTA();
    
    #if DEBUG_TELNET
        telnetLoop();
    #endif
    
    checkNTPSync();
    
    if (now - lastTemperatureControl >= 5000) {
        lastTemperatureControl = now;
        
        if (fermentacaoState.active) {
            brewPiControl.update();
            
            state.currentTemp = tempToFloat(brewPiControl.getBeerTemp());
            state.targetTemp = fermentacaoState.tempTarget;
        }
        
        if (isHTTPOnline()) {
            verificarTargetAtingido();
            enviarEstadoCompletoMySQL();
        }
    }

    if (isHTTPOnline() && fermentacaoState.active && strlen(fermentacaoState.activeId) > 0) {
        int configId = atoi(fermentacaoState.activeId);
        if (configId > 0) {
            sendHeartbeatMySQL(configId);
        }
    }

    static unsigned long lastCheck = 0;
    if (isHTTPOnline() && now - lastCheck >= ACTIVE_CHECK_INTERVAL) {
        lastCheck = now;
        getTargetFermentacao();
        checkPauseOrComplete();
    }

    if (now - lastPhaseCheck >= PHASE_CHECK_INTERVAL) {
        lastPhaseCheck = now;
        verificarTrocaDeFase();
    }
    
    static unsigned long lastSpindel = 0;
    if (now - lastSpindel >= 10000) {
        lastSpindel = now;
        processCloudUpdatesiSpindel();
    }
    
    if (isHTTPOnline()) {
        enviarLeiturasSensoresMySQL();
        
        float tempF, tempG;
        bool sensoresOk = readConfiguredTemperatures(tempF, tempG);
        
        char logBuf[80];
        snprintf(logBuf, sizeof(logBuf), 
                "[DEBUG] Sensores OK: %s, Ferm: %.1f, Geladeira: %.1f",
                sensoresOk ? "SIM" : "NAO", tempF, tempG);
        LOG_SENSORES_MAIN(logBuf);
    }

    if (now - lastSensorCheck >= SENSOR_CHECK_INTERVAL) {
        auto lista = listSensors();
        
        if (lista.empty()) {
            if (isHTTPOnline()) {
                scanAndSendSensors();
                
                String fermenterAddr, fridgeAddr;
                if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
                    
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
        }
        
        lastSensorCheck = now;
    }

    yield();
}