#include "network_manager.h"
#include "wifi_manager.h"
#include "firebase_conexao.h"
#include "ota.h"
#include "fermentacao_firebase.h"
#include "gerenciador_sensores.h"

// =================================================
// ESTADOS INTERNOS
// =================================================

static bool wifiOnline     = false;
static bool firebaseOnline = false;
static bool otaOnline      = false;
static bool sensorsScanned = false;

// VARIÁVEIS PARA CONTROLE DO FIREBASE
static bool firebaseSetupInProgress = false;  // Trava para evitar múltiplas tentativas
static unsigned long lastFirebaseAttempt = 0;  // Última tentativa de setup
static unsigned int firebaseAttemptCount = 0;  // Contador de tentativas
static unsigned long wifiStableSince = 0;
static unsigned long lastWiFiCheck   = 0;

static ESP8266WebServer* webServer = nullptr;

// =================================================
// CONSTANTES
// =================================================

static const unsigned long NET_WIFI_CHECK_INTERVAL = 60000; // 1 min
static const unsigned long NET_WIFI_STABLE_TIME   = 15000; // 15 s
static const unsigned long FIREBASE_RETRY_INTERVAL = 10000; // 10 seg entre tentativas
static const unsigned long FIREBASE_TIMEOUT = 30000; // 30 seg timeout máximo
static const unsigned int MAX_FIREBASE_ATTEMPTS = 3; // Máximo de tentativas consecutivas
static const unsigned long MAX_ATTEMPTS_COOLDOWN = 60000; // 1 min após max tentativas

// =================================================
// FUNÇÃO AUXILIAR PARA RESET DO FIREBASE
// =================================================

static void resetFirebaseState() {
    firebaseOnline = false;
    firebaseSetupInProgress = false;
    firebaseAttemptCount = 0;
    lastFirebaseAttempt = 0;
}

// =================================================
// HELPERS PÚBLICOS
// =================================================

bool isWiFiOnline() {
    return wifiOnline;
}

bool isFirebaseOnline() {
    return firebaseOnline;
}

bool isOTAOnline() {
    return otaOnline;
}

bool canUseFirebase() {
    return wifiOnline && firebaseOnline;
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
    } else {
        Serial.println(F("❌ WiFi offline"));
    }

    resetFirebaseState();
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
            if (firebaseOnline) {
                Serial.println(F("🔥 Firebase offline (WiFi caiu)"));
            }
            resetFirebaseState();
            otaOnline = false;
            sensorsScanned = false;
            return;
        }

        if (!wasOnline && wifiOnline) {
            wifiStableSince = now;
            Serial.println(F("📡 WiFi reconectado"));
            
            // Reset do estado do Firebase quando WiFi reconecta
            resetFirebaseState();
        }
    }

    // =============================================
    // 2. CONTROLE DO FIREBASE COM TRAVAS E INTERVALOS
    // =============================================
    if (wifiOnline && !firebaseOnline) {
        
        // Verifica timeout em progresso
        if (firebaseSetupInProgress) {
            if (now - lastFirebaseAttempt >= FIREBASE_TIMEOUT) {
                Serial.println(F("⏱️  Timeout do Firebase - liberando trava"));
                firebaseSetupInProgress = false;
                firebaseAttemptCount++;
                
                if (firebaseAttemptCount >= MAX_FIREBASE_ATTEMPTS) {
                    Serial.println(F("🔄 Máximo de tentativas do Firebase atingido, aguardando 1 minuto..."));
                    // Reseta após longo período para nova tentativa
                    lastFirebaseAttempt = now;
                }
            }
            // Verifica se ficou pronto DURANTE o setup
            else if (app.ready()) {
                firebaseOnline = true;
                firebaseSetupInProgress = false;
                firebaseAttemptCount = 0;
                
                Serial.println(F("✅ Firebase online e pronto"));
                
                // Processa scan imediatamente
                if (!sensorsScanned) {
                    Serial.println(F("🔍 Scan automático de sensores OneWire"));
                    scanAndSendSensors();
                    sensorsScanned = true;
                }
            }
        }
        // Não está em progresso, verifica se pode iniciar nova tentativa
        else if (!firebaseSetupInProgress) {
            // Calcula tempo mínimo para próxima tentativa
            unsigned long minRetryTime = FIREBASE_RETRY_INTERVAL;
            
            if (firebaseAttemptCount >= MAX_FIREBASE_ATTEMPTS) {
                minRetryTime = MAX_ATTEMPTS_COOLDOWN;
            }
            
            bool canAttemptSetup = 
                (now - wifiStableSince >= NET_WIFI_STABLE_TIME) &&
                (now - lastFirebaseAttempt >= minRetryTime);
            
            if (canAttemptSetup) {
                Serial.print(F("🔥 Tentando setup Firebase (tentativa "));
                Serial.print(firebaseAttemptCount + 1);
                Serial.println(F(")"));
                
                firebaseSetupInProgress = true;
                lastFirebaseAttempt = now;
                
                setupFirebase();
            }
        }
    }

    // =============================================
    // 3. SCAN AUTOMÁTICO DE SENSORES (SE NÃO FEITO)
    // =============================================
    if (firebaseOnline && !sensorsScanned) {
        Serial.println(F("🔍 Scan automático de sensores OneWire"));
        scanAndSendSensors();
        sensorsScanned = true;
    }

    // =============================================
    // 4. CONTROLE INTELIGENTE DE OTA
    // =============================================
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