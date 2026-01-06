#include "network_manager.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "ota.h"
#include "gerenciador_sensores.h"

// =================================================
// ESTADOS INTERNOS
// =================================================

static bool wifiOnline = false;
static bool httpOnline = false;
static bool otaOnline = false;
static bool sensorsScanned = false;

// VARIÁVEIS PARA CONTROLE DO HTTP
static unsigned long lastHttpAttempt = 0;
static unsigned int httpAttemptCount = 0;
static unsigned long wifiStableSince = 0;
static unsigned long lastWiFiCheck = 0;

static ESP8266WebServer* webServer = nullptr;

// =================================================
// CONSTANTES
// =================================================

static const unsigned long NET_WIFI_CHECK_INTERVAL = 60000; // 1 min
static const unsigned long NET_WIFI_STABLE_TIME = 15000; // 15 s
static const unsigned long HTTP_RETRY_INTERVAL = 10000; // 10 seg entre tentativas
static const unsigned int MAX_HTTP_ATTEMPTS = 3; // Máximo de tentativas consecutivas
static const unsigned long MAX_ATTEMPTS_COOLDOWN = 60000; // 1 min após max tentativas

// =================================================
// HELPERS PÚBLICOS
// =================================================

bool isWiFiOnline() {
    return wifiOnline;
}

bool isHTTPOnline() {
    return httpOnline;
}

bool isOTAOnline() {
    return otaOnline;
}

bool canUseHTTP() {
    return wifiOnline && httpOnline;
}

// =================================================
// SETUP
// =================================================

void networkSetup(ESP8266WebServer &server) {
    webServer = &server;

    Serial.println(F("🌐 NetworkManager iniciando..."));

    wifiOnline = setupWiFi(true);

    if (wifiOnline) {
        wifiStableSince = millis();
        Serial.println(F("📡 WiFi online"));
        
        // Testa conexão HTTP
        JsonDocument testDoc;
        if (httpClient.getActiveFermentation(testDoc)) {
            httpOnline = true;
            httpAttemptCount = 0;
            Serial.println(F("✅ HTTP online"));
        } else {
            httpOnline = false;
            Serial.println(F("⚠️  HTTP offline"));
        }
    } else {
        Serial.println(F("❌ WiFi offline"));
        httpOnline = false;
    }

    otaOnline = false;
    sensorsScanned = false;
}

// =================================================
// LOOP PRINCIPAL
// =================================================

void networkLoop() {
    unsigned long now = millis();

    // =============================================
    // 1. MONITORAMENTO DO WI-FI
    // =============================================
    if (now - lastWiFiCheck >= NET_WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = now;

        bool wasOnline = wifiOnline;
        wifiOnline = setupWiFi(false);

        if (!wifiOnline) {
            if (wasOnline) {
                Serial.println(F("⚠️ WiFi caiu, desativando serviços"));
            }

            // Reset estados dependentes do WiFi
            httpOnline = false;
            otaOnline = false;
            sensorsScanned = false;
            httpAttemptCount = 0;
            return;
        }

        if (!wasOnline && wifiOnline) {
            wifiStableSince = now;
            Serial.println(F("📡 WiFi reconectado"));
            
            // Reset do estado HTTP quando WiFi reconecta
            httpOnline = false;
            httpAttemptCount = 0;
        }
    }

    // =============================================
    // 2. CONTROLE DO HTTP COM TRAVAS E INTERVALOS
    // =============================================
    if (wifiOnline && !httpOnline) {
        
        // Calcula tempo mínimo para próxima tentativa
        unsigned long minRetryTime = HTTP_RETRY_INTERVAL;
        
        if (httpAttemptCount >= MAX_HTTP_ATTEMPTS) {
            minRetryTime = MAX_ATTEMPTS_COOLDOWN;
        }
        
        bool canAttemptConnection = 
            (now - wifiStableSince >= NET_WIFI_STABLE_TIME) &&
            (now - lastHttpAttempt >= minRetryTime);
        
        if (canAttemptConnection) {
            Serial.print(F("🌐 Testando HTTP (tentativa "));
            Serial.print(httpAttemptCount + 1);
            Serial.println(F(")"));
            
            lastHttpAttempt = now;
            
            // Testa conexão
            JsonDocument testDoc;
            if (httpClient.getActiveFermentation(testDoc)) {
                httpOnline = true;
                httpAttemptCount = 0;
                Serial.println(F("✅ HTTP online"));
                
                // Processa scan imediatamente
                if (!sensorsScanned) {
                    Serial.println(F("🔍 Scan automático de sensores"));
                    scanAndSendSensors();
                    sensorsScanned = true;
                }
            } else {
                httpAttemptCount++;
                
                if (httpAttemptCount >= MAX_HTTP_ATTEMPTS) {
                    Serial.println(F("🔄 Máximo de tentativas HTTP atingido, aguardando..."));
                }
            }
        }
    }

    // =============================================
    // 3. SCAN AUTOMÁTICO DE SENSORES (SE NÃO FEITO)
    // =============================================
    if (httpOnline && !sensorsScanned) {
        Serial.println(F("🔍 Scan automático de sensores"));
        scanAndSendSensors();
        sensorsScanned = true;
    }

    // =============================================
    // 4. CONTROLE INTELIGENTE DE OTA
    // =============================================
    // Assumindo que fermentacaoState é acessível
    extern struct FermentacaoState fermentacaoState;
    bool fermentacaoAtiva = fermentacaoState.active;

    if (wifiOnline && !fermentacaoAtiva) {
        if (!isOTAEnabled()) {
            setOTAEnabled(true);
            setupOTA(*webServer);
            otaOnline = true;
            Serial.println(F("🟢 OTA habilitado"));
        }
    } else {
        if (isOTAEnabled()) {
            setOTAEnabled(false);
            otaOnline = false;
            Serial.println(F("⛔ OTA pausado (fermentação ativa ou WiFi offline)"));
        }
    }
}