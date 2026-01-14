// main.cpp - Fermentador com MySQL

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#include <time.h>
#include <TZ.h>  // Para fusos horários

// Configuração NTP
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define NTP_SERVER3 "time.google.com"

// Fuso horário do Brasil (Brasília UTC-3)
#define TZ_STRING "BRST3BRDT,M10.3.0/0,M2.3.0/0"  // UTC-3 com horário de verão

#include "secrets.h"
#include "globais.h"
#include "gerenciador_sensores.h"
#include "http_client.h"
#include "ispindel_struct.h"
#include "ispindel_handler.h"
#include "ispindel_envio.h"
#include "controle_fermentacao.h"
#include "controle_temperatura.h"
#include "ota.h"
#include "wifi_manager.h"
#include "network_manager.h"
#include "eeprom_utils.h"
#include "http_commands.h"

ESP8266WebServer server(80);

// === Variáveis de Controle de Tempo === //
unsigned long lastTemperatureControl = 0;
unsigned long lastPhaseCheck = 0;

// ==================== TIMERS ====================
unsigned long lastTempUpdate = 0;
unsigned long lastSensorCheck = 0;

const unsigned long TEMP_UPDATE_INTERVAL = 5000;     // 5 segundos
const unsigned long SENSOR_CHECK_INTERVAL = 30000;   // 30 segundos

// ============================================
// FUNÇÃO DE SETUP DO NTP (UTC PURO)
// ============================================

void setupNTP() {
    Serial.println(F("[NTP] Configurando sincronização de tempo (UTC)..."));

    // UTC puro
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

    Serial.print(F("[NTP] Aguardando sincronização"));
    int timeout = 0;
    time_t now = time(nullptr);

    while (now < 1577836800L && timeout < 100) {
        delay(100);
        Serial.print(".");
        now = time(nullptr);
        timeout++;
    }
    Serial.println();

    if (now > 1577836800L) {
        Serial.println(F("[NTP] ✅ Sincronizado com sucesso! (UTC)"));

        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S UTC", &timeinfo);
        Serial.printf("[NTP] Data/Hora UTC: %s\n", buffer);
    } else {
        Serial.println(F("[NTP] ⚠️ Falha na sincronização"));
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
            Serial.println(F("[NTP] ⚠️ Perdeu sincronização, tentando reconectar..."));
            setupNTP();
        }
    }
}

void sendHeartbeat() {
    static unsigned long lastHeartbeat = 0;
    unsigned long now = millis();
    
    // Envia heartbeat a cada 30 segundos (ou quando há fermentação ativa)
    if (now - lastHeartbeat >= 30000) {
        lastHeartbeat = now;
        
        if (!isHTTPOnline() || !fermentacaoState.active) {
            return; // Não envia se offline ou sem fermentação
        }
        
        HTTPClient http;
        WiFiClient client;
        
        String url = String(SERVER_URL) + "/api/esp/heartbeat.php";
        
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        
        // Prepara dados do heartbeat
        JsonDocument doc;
        doc["config_id"] = fermentacaoState.activeId;
        doc["status"] = "online";
        doc["uptime_seconds"] = millis() / 1000;
        
        // Adiciona temperaturas atuais (se disponíveis)
        float tempFermenter, tempFridge;
        if (readConfiguredTemperatures(tempFermenter, tempFridge)) {
            doc["temp_fermenter"] = tempFermenter;
            doc["temp_fridge"] = tempFridge;
        }
        
        // Adiciona estado dos relés
        doc["cooler_active"] = cooler.estado;
        doc["heater_active"] = heater.estado;
        
        // Adiciona status detalhado do controle
        DetailedControlStatus detailedStatus = getDetailedStatus();
        
        JsonObject controlStatus = doc["control_status"].to<JsonObject>();
        controlStatus["state"] = detailedStatus.stateName;
        controlStatus["is_waiting"] = detailedStatus.isWaiting;
        
        if (detailedStatus.isWaiting && detailedStatus.waitTimeRemaining > 0) {
            controlStatus["wait_seconds"] = detailedStatus.waitTimeRemaining;
            controlStatus["wait_reason"] = detailedStatus.waitReason;
        }
        String json;
        serializeJson(doc, json);
        
        int httpCode = http.POST(json);
        
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            // Heartbeat enviado com sucesso (silencioso)
        } else {
            static unsigned long lastError = 0;
            if (now - lastError >= 300000) { // Log erro a cada 5 min
                Serial.printf("[HEARTBEAT] Erro HTTP: %d\n", httpCode);
                lastError = now;
            }
        }
        
        http.end();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🚀 Iniciando Fermentador Inteligente - MySQL");
    
    // EEPROM
    EEPROM.begin(512);
    
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
    
    // ✅ 1. INICIALIZAÇÃO BÁSICA DOS SENSORES
    setupSensorManager();
    Serial.println("✅ Sensores inicializados");
    
    // ✅ 2. CARREGAR ESTADO SALVO (ANTES de qualquer conexão de rede)
    // Isso garante que temos um estado válido mesmo sem WiFi
    setupActiveListener();
    
    // ✅ 3. CONFIGURAÇÃO DE REDE
    Serial.println("\n📡 Conectando à rede...");
    networkSetup(server);
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n⏰ Configurando NTP...");
        setupNTP();
    }
    
    // ✅ 4. SINCRONIZAR COM SERVIDOR (se online)
    if (isHTTPOnline()) {
        Serial.println(F("\n📡 Enviando sensores detectados ao servidor..."));
        scanAndSendSensors();
        
        // Buscar sensores configurados do servidor
        Serial.println(F("\n📥 Buscando configuração de sensores do servidor..."));
        String fermenterAddr, fridgeAddr;
        
        if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
            bool updated = false;
            
            // Salva fermentador na EEPROM
            if (!fermenterAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR1_NOME, fermenterAddr)) {
                    Serial.println(F("✅ Sensor fermentador salvo na EEPROM"));
                    updated = true;
                }
            }
            
            // Salva geladeira na EEPROM
            if (!fridgeAddr.isEmpty()) {
                if (saveSensorToEEPROM(SENSOR2_NOME, fridgeAddr)) {
                    Serial.println(F("✅ Sensor geladeira salvo na EEPROM"));
                    updated = true;
                }
            }
            
            if (updated) {
                Serial.println(F("✅ Sensores sincronizados do servidor!"));
                
                // ✅ RECARREGAR SENSORES após atualização
                setupSensorManager();
            }
        } else {
            Serial.println(F("⚠️ Nenhum sensor configurado no servidor"));
        }
        
        // ✅ 5. VERIFICAR FERMENTAÇÃO ATIVA NO SERVIDOR
        // Se acabamos de carregar um estado local, verificar se ainda está válido
        if (fermentacaoState.active) {
            Serial.println(F("\n🔍 Verificando se fermentação ainda está ativa no servidor..."));
            getTargetFermentacao();  // Esta função também reseta o PID se necessário
        }
    }
    
    // ✅ 6. LISTAR SENSORES CONFIGURADOS
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

    // ✅ 7. WEBSERVER / ISPINDEL
    setupSpindelRoutes(server);
    setupOTA(server);
    server.begin();
    Serial.println("🌐 Servidor Web ativo");
    
    // ✅ 8. VALIDAÇÃO FINAL DO ESTADO
    // Garantir temperatura segura se não houver fermentação ativa
    if (!fermentacaoState.active) {
        // Se não há fermentação ativa, garantir temperatura padrão
        if (state.targetTemp != DEFAULT_TEMPERATURE) {
            Serial.printf("[Setup] ⚠️  Ajustando temperatura para padrão: %.1f°C\n", DEFAULT_TEMPERATURE);
            updateTargetTemperature(DEFAULT_TEMPERATURE);
            resetPIDState();  // Reset adicional por segurança
        }
    } else {
        // Se há fermentação ativa, validar temperatura
        if (fermentacaoState.tempTarget < MIN_SAFE_TEMPERATURE || 
            fermentacaoState.tempTarget > MAX_SAFE_TEMPERATURE) {
            Serial.printf("[Setup] ⚠️  Temperatura alvo inválida: %.1f°C, ajustando para %.1f°C\n",
                         fermentacaoState.tempTarget, DEFAULT_TEMPERATURE);
            updateTargetTemperature(DEFAULT_TEMPERATURE);
            resetPIDState();
        }
    }
    
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
        
        // Mostra tempo decorrido se etapa estiver ativa
        if (fermentacaoState.stageStartEpoch > 0) {
            time_t now = time(nullptr);
            if (now > 1000000000L) {
                float elapsedH = difftime(now, fermentacaoState.stageStartEpoch) / 3600.0;
                Serial.printf("Tempo decorrido: %.1f horas\n", elapsedH);
            }
        }
        
        // ✅ LOG ESPECÍFICO PARA PID
        Serial.println("[PID] 🔄 Sistema carregado com fermentação ativa - PID pronto");
    } else {
        Serial.printf("Temperatura padrão: %.1f°C\n", DEFAULT_TEMPERATURE);
        Serial.println("[PID] 🛑 Sistema em standby - PID resetado");
    }
    Serial.println("==============================================");
}

void loop() {
    unsigned long now = millis();
    
    // ⚠️ PRIMEIRO: Verifica comandos seriais
    checkSerialCommands();
    
    // === Verifica comandos HTTP a cada 10 segundos ===
    static unsigned long lastCommandCheck = 0;
    if (now - lastCommandCheck >= 10000) {  // 10 segundos
        lastCommandCheck = now;
        checkPendingCommands();
    }
    
    // === Network Manager ===
    networkLoop();
    
    // Envia heartbeat periódico para o site
    sendHeartbeat();

    // WebServer (OTA + iSpindel)
    server.handleClient();
    handleOTA();
    
    // ⏰ Verifica NTP periodicamente (tenta reconectar se perdeu sync)
    checkNTPSync();
    
    // 🔁 Verificação HTTP
    static unsigned long lastCheck = 0;
    if (isHTTPOnline() && now - lastCheck >= ACTIVE_CHECK_INTERVAL) {
        lastCheck = now;
        getTargetFermentacao();
        
        // Verifica se foi pausada/concluída pelo site
        checkPauseOrComplete();
    }

    // 🍺 Troca de fase (sempre roda)
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
            enviarLeiturasSensores();
            verificarTargetAtingido();
            
            // Envia estado do controlador
            httpClient.updateControlState(
                fermentacaoState.activeId,
                state.targetTemp,
                cooler.estado,
                heater.estado
            );

            // NOVO: ENVIA ESTADO COMPLETO A CADA 30s
            enviarEstadoCompleto();
        }
    }
    
    // ==================== ATUALIZAÇÃO DE TEMPERATURAS RÁPIDA ====================
    if (now - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
        float tempFermenter, tempFridge;
        
        if (readConfiguredTemperatures(tempFermenter, tempFridge)) {
            if (isHTTPOnline()) {
                if (!httpClient.updateCurrentTemperatures(tempFermenter, tempFridge)) {
                    static unsigned long lastWarning = 0;
                    if (now - lastWarning >= 60000) {
                        Serial.println(F("⚠️ Erro ao atualizar temperaturas"));
                        lastWarning = now;
                    }
                }
            }
        }
        
        lastTempUpdate = now;
    }
    
    // ==================== VERIFICAÇÃO PERIÓDICA DE SENSORES ====================
    if (now - lastSensorCheck >= SENSOR_CHECK_INTERVAL) {
        auto lista = listSensors();
        
        if (lista.empty()) {
            if (isHTTPOnline()) {
                scanAndSendSensors();
                
                String fermenterAddr, fridgeAddr;
                if (httpClient.getAssignedSensors(fermenterAddr, fridgeAddr)) {
                    Serial.println(F("📥 Configuração encontrada no servidor!"));
                    
                    if (!fermenterAddr.isEmpty()) {
                        saveSensorToEEPROM(SENSOR1_NOME, fermenterAddr);
                    }
                    
                    if (!fridgeAddr.isEmpty()) {
                        saveSensorToEEPROM(SENSOR2_NOME, fridgeAddr);
                    }
                }
            }
        } else {
            // Mantém o log periódico (importante para monitoramento)
            static unsigned long lastSuccessLog = 0;
            if (now - lastSuccessLog >= 300000) {
                Serial.printf("✓ %d sensor(es) configurado(s)\n", lista.size());
                lastSuccessLog = now;
            }
        }
        
        lastSensorCheck = now;
    }

    yield();
}