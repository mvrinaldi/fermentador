// controle_temperatura_brewpi.cpp - Adaptação do BrewPiLess para Fermentador
#include <Arduino.h>
#include <DallasTemperature.h>
#include "globais.h"
#include "estruturas.h"
#include "definitions.h"
#include "http_client.h"
#include "controle_temperatura.h"  // Para DetailedControlStatus

// ========================================
// ESTRUTURAS DE DADOS DO BREWPILESS
// ========================================

// Constantes de controle (adaptado do BrewPiLess)
struct BrewPiControlConstants {
    // Parâmetros PID
    float Kp;                      // Ganho proporcional (padrão: 5.0)
    float Ki;                      // Ganho integral (padrão: 0.25)
    float Kd;                      // Ganho derivativo (padrão: -1.5)
    float iMaxError;               // Erro máximo para ação integral (padrão: 0.5°C)
    
    // Faixas de operação (histerese)
    float idleRangeHigh;           // +1.0°C
    float idleRangeLow;            // -1.0°C
    
    // Alvos de detecção de pico
    float heatingTargetUpper;      // +0.3°C
    float heatingTargetLower;      // -0.2°C
    float coolingTargetUpper;      // +0.2°C
    float coolingTargetLower;      // -0.3°C
    
    // Tempos de proteção (em segundos)
    uint16_t maxHeatTimeForEstimate;   // 600s (10min)
    uint16_t maxCoolTimeForEstimate;   // 1200s (20min)
    uint16_t minCoolTime;              // 180s (3min)
    uint16_t minCoolIdleTime;          // 300s (5min)
    uint16_t minHeatTime;              // 180s (3min)
    uint16_t minHeatIdleTime;          // 300s (5min)
    uint16_t mutexDeadTime;            // 600s (10min)
    
    // Estimadores de overshoot (em °C por hora)
    float coolEstimator;           // 5.0°C/h inicial
    float heatEstimator;           // 0.2°C/h inicial
    
    // PID Max
    float pidMax;                  // ±10°C
};

// Variáveis de controle (adaptado do BrewPiLess)
struct BrewPiControlVariables {
    float beerDiff;                // Erro atual
    float diffIntegral;            // Acumulador integral
    float beerSlope;               // Taxa de mudança
    float p;                       // Componente proporcional
    float i;                       // Componente integral
    float d;                       // Componente derivativo
    float estimatedPeak;           // Pico estimado
    float negPeakEstimate;         // Última estimativa de pico negativo
    float posPeakEstimate;         // Última estimativa de pico positivo
    float negPeak;                 // Último pico negativo detectado
    float posPeak;                 // Último pico positivo detectado
};

// Estados da máquina de estados
enum BrewPiState {
    IDLE,
    STATE_OFF,
    HEATING,
    COOLING,
    WAITING_TO_COOL,
    WAITING_TO_HEAT,
    WAITING_FOR_PEAK_DETECT,
    COOLING_MIN_TIME,
    HEATING_MIN_TIME
};

// ========================================
// VARIÁVEIS GLOBAIS DO CONTROLE
// ========================================

static BrewPiControlConstants cc;
static BrewPiControlVariables cv;
static BrewPiState controlState = IDLE;

// Tempos de controle
static unsigned long lastIdleTime = 0;
static unsigned long lastHeatTime = 0;
static unsigned long lastCoolTime = 0;
static uint16_t waitTime = 0;

// Detecção de picos
static bool doPosPeakDetect = false;
static bool doNegPeakDetect = false;

// Filtros de temperatura
static float fridgeFastFiltered = 0;
static float fridgeSlowFiltered = 0;
static float beerFastFiltered = 0;
static float beerSlowFiltered = 0;
static float beerSlopeValue = 0;

// Histórico para filtros
#define FILTER_HISTORY_SIZE 8
static float fridgeFastHistory[FILTER_HISTORY_SIZE] = {0};
static float fridgeSlowHistory[FILTER_HISTORY_SIZE] = {0};
static float beerFastHistory[FILTER_HISTORY_SIZE] = {0};
static float beerSlowHistory[FILTER_HISTORY_SIZE] = {0};
static uint8_t filterIndex = 0;

// Controle de tempo para slope
static unsigned long lastSlopeUpdate = 0;
static float lastSlowFilterValue = 0;
static uint8_t slopeUpdateCounter = 0;

// Flag de inicialização
static bool controlInitialized = false;

// ========================================
// FUNÇÕES DE CONVERSÃO DE TEMPERATURA
// ========================================

// Converte temperatura interna (formato fixo do BrewPi) para float
inline float tempToFloat(int16_t temp) {
    // BrewPi usa fixed-point com 9 bits fracionários
    // Offset: -48°C (C_OFFSET = -24576)
    return ((float)temp - (-24576)) / 512.0f;
}

// Converte float para temperatura interna
inline int16_t floatToTemp(float temp) {
    return (int16_t)((temp * 512.0f) + (-24576));
}

// ========================================
// INICIALIZAÇÃO
// ========================================

void loadDefaultBrewPiConstants() {
    // Parâmetros PID
    cc.Kp = 5.0;
    cc.Ki = 0.25;
    cc.Kd = -1.5;
    cc.iMaxError = 0.5;
    
    // Faixas de operação - significa... quando chegar aqui, para!
    cc.idleRangeHigh = 0.3; // BrewPi original tinha 1.0 - Claude sugeriu 0.5
    cc.idleRangeLow = -0.3; // BrewPi original tinha -1.0 - Claude sugeriu -0.5
    
    // Alvos de detecção de pico (MAIS TOLERANTES)
    cc.heatingTargetUpper = 0.3;   // ← Era 0.3
    cc.heatingTargetLower = -0.2;  // ← Era -0.2
    cc.coolingTargetUpper = 0.2;   // ← Era 0.2
    cc.coolingTargetLower = -0.3;  // ← Era -0.3
    
    // ✅ VALORES CORRIGIDOS PARA FERMENTAÇÃO:
    cc.maxHeatTimeForEstimate = 600;    // 10min
    cc.maxCoolTimeForEstimate = 1200;   // 20min
    
    // 🔥 CORREÇÕES CRÍTICAS:
    cc.minCoolTime = 600;               // 10min (era 3min) - Geladeira precisa tempo!
    cc.minCoolIdleTime = 900;           // 15min (era 5min) - Intervalo entre ciclos
    cc.minHeatTime = 300;               // 5min (era 3min)
    cc.minHeatIdleTime = 600;           // 10min (era 5min)
    cc.mutexDeadTime = 900;             // 15min (era 10min) - Proteção heat↔cool
    
    // Estimadores mais conservadores para baixa temperatura
    cc.coolEstimator = 2.0;             // ← Era 5.0°C/h - muito otimista!
    cc.heatEstimator = 0.3;             // ← Era 0.2°C/h
    cc.pidMax = 10.0;
}

void resetBrewPiControl() {
    cv.beerDiff = 0;
    cv.diffIntegral = 0;
    cv.beerSlope = 0;
    cv.p = 0;
    cv.i = 0;
    cv.d = 0;
    cv.estimatedPeak = 0;
    cv.negPeakEstimate = 0;
    cv.posPeakEstimate = 0;
    cv.negPeak = 0;
    cv.posPeak = 0;
    
    controlState = IDLE;
    doPosPeakDetect = false;
    doNegPeakDetect = false;
    waitTime = 0;
    
    // Reset de tempos
    lastIdleTime = millis() / 1000;
    lastHeatTime = 0;
    lastCoolTime = 0;
    
    Serial.println(F("[BrewPi] 🔄 Estado do controle resetado"));
}

// ========================================
// FILTROS DE TEMPERATURA
// ========================================

void initFilters(float initialTemp) {
    // Inicializa todos os filtros com a temperatura inicial
    for (int i = 0; i < FILTER_HISTORY_SIZE; i++) {
        fridgeFastHistory[i] = initialTemp;
        fridgeSlowHistory[i] = initialTemp;
        beerFastHistory[i] = initialTemp;
        beerSlowHistory[i] = initialTemp;
    }
    
    fridgeFastFiltered = initialTemp;
    fridgeSlowFiltered = initialTemp;
    beerFastFiltered = initialTemp;
    beerSlowFiltered = initialTemp;
    beerSlopeValue = 0;
    
    lastSlowFilterValue = initialTemp;
    lastSlopeUpdate = millis();
    slopeUpdateCounter = 3;
    
    Serial.printf("[BrewPi] ✅ Filtros inicializados com %.2f°C\n", initialTemp);
}

// Filtro de média móvel simples
float applyFilter(float newValue, float* history, uint8_t size) {
    // Adiciona novo valor
    history[filterIndex % size] = newValue;
    
    // Calcula média
    float sum = 0;
    for (uint8_t i = 0; i < size; i++) {
        sum += history[i];
    }
    
    return sum / size;
}

void updateFilters(float tempFridge, float tempBeer) {
    // Atualiza índice circular
    filterIndex++;
    
    // Filtros rápidos (2 amostras)
    fridgeFastFiltered = applyFilter(tempFridge, fridgeFastHistory, 2);
    beerFastFiltered = applyFilter(tempBeer, beerFastHistory, 4);
    
    // Filtros lentos (8 amostras)
    fridgeSlowFiltered = applyFilter(tempFridge, fridgeSlowHistory, 8);
    beerSlowFiltered = applyFilter(tempBeer, beerSlowHistory, 8);
    
    // Atualiza slope a cada 3 amostras (aproximadamente 15 segundos)
    slopeUpdateCounter--;
    if (slopeUpdateCounter == 0) {
        slopeUpdateCounter = 3;
        
        unsigned long now = millis();
        float elapsedSeconds = (now - lastSlopeUpdate) / 1000.0f;
        
        if (elapsedSeconds > 0) {
            // Calcula diferença
            float diff = beerSlowFiltered - lastSlowFilterValue;
            
            // Slope em °C por hora
            beerSlopeValue = (diff / elapsedSeconds) * 3600.0f;
            
            // Limita para prevenir valores absurdos
            if (beerSlopeValue > 10.0f) beerSlopeValue = 10.0f;
            if (beerSlopeValue < -10.0f) beerSlopeValue = -10.0f;
        }
        
        lastSlowFilterValue = beerSlowFiltered;
        lastSlopeUpdate = now;
    }
}

// ========================================
// FUNÇÕES DE TEMPO
// ========================================

uint16_t getCurrentSeconds() {
    return millis() / 1000;
}

uint16_t timeSince(uint16_t timestamp) {
    uint16_t current = getCurrentSeconds();
    if (current >= timestamp) {
        return current - timestamp;
    } else {
        // Overflow handling (wrap around a cada ~18 horas)
        return (current + 65536) - timestamp;
    }
}

void updateWaitTime(uint16_t requiredTime, uint16_t timeSinceLast) {
    if (timeSinceLast < requiredTime) {
        uint16_t newWait = requiredTime - timeSinceLast;
        if (newWait > waitTime) {
            waitTime = newWait;
        }
    }
}

// ========================================
// CONTROLE PID COM ANTI-WINDUP
// ========================================

void updatePID(float setpoint, float currentTemp) {
    static uint8_t integralUpdateCounter = 0;
    
    // Calcula erro
    cv.beerDiff = setpoint - beerSlowFiltered;
    cv.beerSlope = beerSlopeValue;
    
    // Atualiza integral a cada 60 ciclos (aprox. 5 minutos)
    integralUpdateCounter++;
    if (integralUpdateCounter >= 60) {
        integralUpdateCounter = 0;
        
        float integratorUpdate = cv.beerDiff;
        
        // ANTI-WINDUP: Só atualiza integral em IDLE
        if (controlState != IDLE) {
            integratorUpdate = 0;
        }
        else if (fabs(integratorUpdate) < cc.iMaxError) {
            bool updateSign = (integratorUpdate > 0);
            bool integratorSign = (cv.diffIntegral > 0);
            
            if (updateSign == integratorSign) {
                // Mesmo sinal - verificar saturação
                float fridgeSetting = setpoint + cv.p + cv.i + cv.d;
                
                // Limites de saturação
                if (fridgeSetting >= 30.0f) integratorUpdate = 0;
                if (fridgeSetting <= 1.0f) integratorUpdate = 0;
                if ((fridgeSetting - setpoint) >= cc.pidMax) integratorUpdate = 0;
                if ((setpoint - fridgeSetting) >= cc.pidMax) integratorUpdate = 0;
                
                // Verificar saturação física
                if (!updateSign && (fridgeFastFiltered > (fridgeSetting + 2.0f))) {
                    integratorUpdate = 0;
                }
                if (updateSign && (fridgeFastFiltered < (fridgeSetting - 2.0f))) {
                    integratorUpdate = 0;
                }
            }
            else {
                // Sinais opostos - diminuir integral mais rápido
                integratorUpdate = integratorUpdate * 2.0f;
            }
        }
        else {
            // Longe do setpoint - resetar integral gradualmente
            integratorUpdate = -(cv.diffIntegral / 8.0f);
        }
        
        cv.diffIntegral = constrain(cv.diffIntegral + integratorUpdate, -100.0f, 100.0f);
    }
    
    // Calcular componentes PID
    cv.p = cc.Kp * cv.beerDiff;
    cv.i = cc.Ki * cv.diffIntegral;
    cv.d = cc.Kd * cv.beerSlope;
    
    // Calcular novo setpoint da geladeira
    float newFridgeSetting = setpoint + cv.p + cv.i + cv.d;
    
    // Limites dinâmicos
    float lowerBound = (setpoint <= 1.0f + cc.pidMax) ? 1.0f : setpoint - cc.pidMax;
    float upperBound = (setpoint >= 30.0f - cc.pidMax) ? 30.0f : setpoint + cc.pidMax;
    
    fridgeSlowFiltered = constrain(newFridgeSetting, lowerBound, upperBound);
    
    // Debug do PID (a cada 30 segundos)
    static unsigned long lastPIDDebug = 0;
    if (millis() - lastPIDDebug >= 30000) {
        lastPIDDebug = millis();
        Serial.println(F("\n━━━━━━━━ PID DEBUG ━━━━━━━━"));
        Serial.printf("Setpoint: %.2f°C\n", setpoint);
        Serial.printf("Beer Temp: %.2f°C (filtered: %.2f°C)\n", currentTemp, beerSlowFiltered);
        Serial.printf("Erro: %.2f°C\n", cv.beerDiff);
        Serial.printf("Slope: %.3f °C/h\n", cv.beerSlope);
        Serial.printf("P: %.3f, I: %.3f, D: %.3f\n", cv.p, cv.i, cv.d);
        Serial.printf("Integral: %.3f\n", cv.diffIntegral);
        Serial.printf("Fridge Setting: %.2f°C\n", fridgeSlowFiltered);
        Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
    }
}

// ========================================
// FUNÇÕES AUXILIARES DE ESTADO
// ========================================

bool stateIsCooling() {
    return (controlState == COOLING || controlState == COOLING_MIN_TIME);
}

bool stateIsHeating() {
    return (controlState == HEATING || controlState == HEATING_MIN_TIME);
}

// ========================================
// DETECÇÃO DE PICOS
// ========================================

void detectPeaks() {
    // Detecta pico positivo (após aquecimento)
    if (doPosPeakDetect && !stateIsHeating()) {
        uint16_t sinceHeating = timeSince(lastHeatTime);
        
        if (sinceHeating > 900) { // 15 minutos
            float peak = fridgeFastFiltered;
            float error = peak - cv.posPeakEstimate;
            
            if (error > cc.heatingTargetUpper) {
                // Overshoot maior que esperado
                cc.heatEstimator *= 1.2f; // Aumenta 20%
                if (cc.heatEstimator < 0.05f) cc.heatEstimator = 0.05f;
                
                Serial.printf("[BrewPi] 🔺 Pico positivo: %.2f°C (esperado: %.2f°C)\n", 
                             peak, cv.posPeakEstimate);
                Serial.printf("[BrewPi] ↑ Estimador aquecimento: %.3f°C/h\n", cc.heatEstimator);
            }
            else if (error < cc.heatingTargetLower) {
                // Overshoot menor que esperado
                cc.heatEstimator *= 0.833f; // Diminui 16.7%
                
                Serial.printf("[BrewPi] 🔺 Pico positivo: %.2f°C (esperado: %.2f°C)\n", 
                             peak, cv.posPeakEstimate);
                Serial.printf("[BrewPi] ↓ Estimador aquecimento: %.3f°C/h\n", cc.heatEstimator);
            }
            
            cv.posPeak = peak;
            doPosPeakDetect = false;
        }
    }
    
    // Detecta pico negativo (após resfriamento)
    if (doNegPeakDetect && !stateIsCooling()) {
        uint16_t sinceCooling = timeSince(lastCoolTime);
        
        if (sinceCooling > 1800) { // 30 minutos
            float peak = fridgeFastFiltered;
            float error = peak - cv.negPeakEstimate;
            
            if (error < cc.coolingTargetLower) {
                // Overshoot maior que esperado
                cc.coolEstimator *= 1.2f;
                if (cc.coolEstimator < 0.05f) cc.coolEstimator = 0.05f;
                
                Serial.printf("[BrewPi] 🔻 Pico negativo: %.2f°C (esperado: %.2f°C)\n", 
                             peak, cv.negPeakEstimate);
                Serial.printf("[BrewPi] ↑ Estimador resfriamento: %.3f°C/h\n", cc.coolEstimator);
            }
            else if (error > cc.coolingTargetUpper) {
                // Overshoot menor que esperado
                cc.coolEstimator *= 0.833f;
                
                Serial.printf("[BrewPi] 🔻 Pico negativo: %.2f°C (esperado: %.2f°C)\n", 
                             peak, cv.negPeakEstimate);
                Serial.printf("[BrewPi] ↓ Estimador resfriamento: %.3f°C/h\n", cc.coolEstimator);
            }
            
            cv.negPeak = peak;
            doNegPeakDetect = false;
        }
    }
}

// ========================================
// ESTIMAÇÃO DE PICO
// ========================================

void updateEstimatedPeak(uint16_t timeLimit, float estimator, uint16_t sinceIdle) {
    uint16_t activeTime = min(timeLimit, sinceIdle);
    
    // Overshoot em °C (estimador está em °C/hora)
    float estimatedOvershoot = (estimator * activeTime) / 3600.0f;
    
    if (stateIsCooling()) {
        estimatedOvershoot = -estimatedOvershoot;
    }
    
    cv.estimatedPeak = fridgeFastFiltered + estimatedOvershoot;
}

// ========================================
// MÁQUINA DE ESTADOS
// ========================================

void updateControlState(float setpoint) {
    uint16_t sinceIdle = timeSince(lastIdleTime);
    uint16_t sinceCooling = timeSince(lastCoolTime);
    uint16_t sinceHeating = timeSince(lastHeatTime);
    uint16_t nowSeconds = getCurrentSeconds();
    
    switch (controlState) {
        case STATE_OFF:
            lastIdleTime = nowSeconds;
            break;
            
        case IDLE:
        case WAITING_TO_COOL:
        case WAITING_TO_HEAT:
        case WAITING_FOR_PEAK_DETECT:
            lastIdleTime = nowSeconds;
            waitTime = 0;
            
            // ✅ CORREÇÃO: Compara temperatura da CERVEJA com SETPOINT
            // (não a temperatura da geladeira com o alvo PID)
            
            // Temperatura muito alta - precisa resfriar
            if (beerFastFiltered > (setpoint + cc.idleRangeHigh)) {
                updateWaitTime(cc.mutexDeadTime, sinceHeating);
                updateWaitTime(cc.minCoolIdleTime, sinceCooling);
                
                if (waitTime > 0) {
                    controlState = WAITING_TO_COOL;
                } else {
                    controlState = COOLING;
                }
            }
            // Temperatura muito baixa - precisa aquecer
            else if (beerFastFiltered < (setpoint + cc.idleRangeLow)) {
                updateWaitTime(cc.mutexDeadTime, sinceCooling);
                updateWaitTime(cc.minHeatIdleTime, sinceHeating);
                
                if (waitTime > 0) {
                    controlState = WAITING_TO_HEAT;
                } else {
                    controlState = HEATING;
                }
            }
            else {
                controlState = IDLE;
            }
            
            // Se vai aquecer/resfriar mas detecção de pico está ativa
            if ((controlState == HEATING || controlState == COOLING) &&
                (doNegPeakDetect || doPosPeakDetect)) {
                controlState = WAITING_FOR_PEAK_DETECT;
            }
            break;
            
        case COOLING:
        case COOLING_MIN_TIME:
            doNegPeakDetect = true;
            lastCoolTime = nowSeconds;
            updateEstimatedPeak(cc.maxCoolTimeForEstimate, cc.coolEstimator, sinceIdle);
            controlState = COOLING;
            
            // ✅ Para quando cerveja atingir temperatura (com margem de segurança)
            if (cv.estimatedPeak <= fridgeSlowFiltered ||
                beerFastFiltered < (setpoint - 0.1f)) {  // ← Margem maior para evitar overshooting
                
                if (sinceIdle > cc.minCoolTime) {
                    cv.negPeakEstimate = cv.estimatedPeak;
                    controlState = IDLE;
                } else {
                    controlState = COOLING_MIN_TIME;
                }
            }
            break;
            
        case HEATING:
        case HEATING_MIN_TIME:
            doPosPeakDetect = true;
            lastHeatTime = nowSeconds;
            updateEstimatedPeak(cc.maxHeatTimeForEstimate, cc.heatEstimator, sinceIdle);
            controlState = HEATING;
            
            // ✅ Para quando cerveja atingir temperatura
            if (cv.estimatedPeak >= fridgeSlowFiltered ||
                beerFastFiltered > (setpoint + 0.1f)) {
                
                if (sinceIdle > cc.minHeatTime) {
                    cv.posPeakEstimate = cv.estimatedPeak;
                    controlState = IDLE;
                } else {
                    controlState = HEATING_MIN_TIME;
                }
            }
            break;
    }
}

// ========================================
// ATUALIZAÇÃO DE SAÍDAS
// ========================================

void updateOutputs() {
    bool shouldCool = stateIsCooling();
    bool shouldHeat = stateIsHeating();
    
    // Atualiza cooler
    if (cooler.estado != shouldCool) {
        cooler.estado = shouldCool;
        cooler.atualizar();
        
        if (shouldCool) {
            Serial.println(F("[BrewPi] ❄️  COOLER LIGADO"));
        } else {
            Serial.println(F("[BrewPi] ❄️  COOLER DESLIGADO"));
        }
    }
    
    // Atualiza heater
    if (heater.estado != shouldHeat) {
        heater.estado = shouldHeat;
        heater.atualizar();
        
        if (shouldHeat) {
            Serial.println(F("[BrewPi] 🔥 HEATER LIGADO"));
        } else {
            Serial.println(F("[BrewPi] 🔥 HEATER DESLIGADO"));
        }
    }
}

// ========================================
// FUNÇÃO PRINCIPAL DE CONTROLE
// ========================================

void controle_temperatura() {
    unsigned long now = millis();
    static unsigned long lastControl = 0;
    
    // Executa controle a cada 5 segundos
    if (now - lastControl < 5000) return;
    lastControl = now;
    
    // Se não há fermentação ativa, desliga tudo
    if (!fermentacaoState.active) {
        if (cooler.estado || heater.estado) {
            cooler.estado = false;
            heater.estado = false;
            cooler.atualizar();
            heater.atualizar();
        }
        controlState = STATE_OFF;
        return;
    }
    
    // ========================================
    // LEITURA DOS SENSORES
    // ========================================
    sensors.requestTemperatures();
    
    // DELAY para conseguir ler sensores (sensor DS18B20 precisa de tempo)
    delay(750);

    float tempFerm = sensors.getTempCByIndex(0);
    float tempGel = sensors.getTempCByIndex(1);
    
    // Valida leituras
    if (tempFerm == DEVICE_DISCONNECTED_C || tempFerm < -20 || tempFerm > 50) {
        Serial.println(F("[BrewPi] ❌ Sensor fermentador com erro"));
        controlState = IDLE;
        return;
    }
    
    if (tempGel == DEVICE_DISCONNECTED_C || tempGel < -20 || tempGel > 50) {
        Serial.println(F("[BrewPi] ❌ Sensor geladeira com erro"));
        controlState = IDLE;
        return;
    }
    
    // Atualiza temperatura do sistema
    state.currentTemp = tempFerm;

    state.lastTempUpdate = millis(); 
    
    // ========================================
    // INICIALIZAÇÃO DOS FILTROS
    // ========================================
    if (!controlInitialized) {
        loadDefaultBrewPiConstants();
        initFilters(tempFerm);
        resetBrewPiControl();
        controlInitialized = true;
        Serial.println(F("[BrewPi] ✅ Sistema inicializado"));
    }
    
    // ========================================
    // ATUALIZAÇÃO DOS FILTROS
    // ========================================
    updateFilters(tempGel, tempFerm);
    
    // ========================================
    // CONTROLE PID
    // ========================================
    float setpoint = fermentacaoState.tempTarget;
    updatePID(setpoint, tempFerm);
    
    // ========================================
    // MÁQUINA DE ESTADOS
    // ========================================
    updateControlState(setpoint);
    
    // ========================================
    // DETECÇÃO DE PICOS
    // ========================================
    detectPeaks();
    
    // ========================================
    // ATUALIZAÇÃO DE SAÍDAS
    // ========================================
    updateOutputs();
    
    // ✅ LOG DETALHADO A CADA CICLO (não apenas a cada 60s)
    static unsigned long lastDetailedLog = 0;
    if (now - lastDetailedLog >= 30000) {  // A cada 30 segundos
        lastDetailedLog = now;
        
        Serial.println(F("\n╔══════════════════════════════════════╗"));
        Serial.println(F("║       DEBUG TEMPERATURA CRÍTICO      ║"));
        Serial.println(F("╠══════════════════════════════════════╣"));
        
        const char* stateStr = "";
        switch (controlState) {
            case IDLE: stateStr = "IDLE"; break;
            case STATE_OFF: stateStr = "OFF"; break;
            case HEATING: stateStr = "HEATING"; break;
            case COOLING: stateStr = "COOLING"; break;
            case WAITING_TO_COOL: stateStr = "WAIT_COOL"; break;
            case WAITING_TO_HEAT: stateStr = "WAIT_HEAT"; break;
            case WAITING_FOR_PEAK_DETECT: stateStr = "WAIT_PEAK"; break;
            case COOLING_MIN_TIME: stateStr = "COOL_MIN"; break;
            case HEATING_MIN_TIME: stateStr = "HEAT_MIN"; break;
        }
        
        Serial.printf("║ Estado Máquina: %-20s ║\n", stateStr);
        Serial.printf("║ Temp Ferm:      %6.2f°C              ║\n", tempFerm);
        Serial.printf("║ Temp Gel:       %6.2f°C              ║\n", tempGel);
        Serial.printf("║ Alvo:           %6.2f°C              ║\n", setpoint);
        Serial.printf("║ Erro:           %+6.2f°C             ║\n", cv.beerDiff);
        Serial.println(F("╠══════════════════════════════════════╣"));
        Serial.printf("║ Cooler:         %-20s ║\n", cooler.estado ? "LIGADO" : "DESLIGADO");
        Serial.printf("║ Heater:         %-20s ║\n", heater.estado ? "LIGADO" : "DESLIGADO");
        Serial.println(F("╠══════════════════════════════════════╣"));
        
        // Tempos desde última ação
        uint16_t sinceIdle = timeSince(lastIdleTime);
        uint16_t sinceCool = timeSince(lastCoolTime);
        uint16_t sinceHeat = timeSince(lastHeatTime);
        
        Serial.printf("║ Desde IDLE:     %5us (%3umin)        ║\n", 
                     sinceIdle, sinceIdle/60);
        Serial.printf("║ Desde COOL:     %5us (%3umin)        ║\n", 
                     sinceCool, sinceCool/60);
        Serial.printf("║ Desde HEAT:     %5us (%3umin)        ║\n", 
                     sinceHeat, sinceHeat/60);
        
        // Estimadores
        Serial.println(F("╠══════════════════════════════════════╣"));
        Serial.printf("║ Cool Estimator: %6.2f°C/h           ║\n", cc.coolEstimator);
        Serial.printf("║ Heat Estimator: %6.2f°C/h           ║\n", cc.heatEstimator);
        Serial.printf("║ Est. Peak:      %+6.2f°C             ║\n", cv.estimatedPeak);
        
        Serial.println(F("╚══════════════════════════════════════╝\n"));
    }
    
    // ========================================
    // LOG PERIÓDICO
    // ========================================
    static unsigned long lastLog = 0;
    if (now - lastLog >= 60000) {
        lastLog = now;
        
        Serial.println(F("\n╔══════════════════════════════════════╗"));
        Serial.println(F("║       STATUS DO CONTROLE BREWPI      ║"));
        Serial.println(F("╠══════════════════════════════════════╣"));
        Serial.printf("║ Fermentador: %6.2f°C (filt: %6.2f) ║\n", 
                     tempFerm, beerSlowFiltered);
        Serial.printf("║ Geladeira:   %6.2f°C (filt: %6.2f) ║\n", 
                     tempGel, fridgeSlowFiltered);
        Serial.printf("║ Alvo:        %6.2f°C                ║\n", setpoint);
        Serial.printf("║ Erro:        %+6.2f°C                ║\n", cv.beerDiff);
        Serial.printf("║ Slope:       %+6.3f°C/h             ║\n", cv.beerSlope);
        Serial.println(F("╠══════════════════════════════════════╣"));
        
        const char* stateStr = "";
        switch (controlState) {
            case IDLE: stateStr = "IDLE"; break;
            case STATE_OFF: stateStr = "OFF"; break;
            case HEATING: stateStr = "HEATING"; break;
            case COOLING: stateStr = "COOLING"; break;
            case WAITING_TO_COOL: stateStr = "WAIT_COOL"; break;
            case WAITING_TO_HEAT: stateStr = "WAIT_HEAT"; break;
            case WAITING_FOR_PEAK_DETECT: stateStr = "WAIT_PEAK"; break;
            case COOLING_MIN_TIME: stateStr = "COOL_MIN"; break;
            case HEATING_MIN_TIME: stateStr = "HEAT_MIN"; break;
        }
        
        Serial.printf("║ Estado:      %-24s ║\n", stateStr);
        Serial.printf("║ Cooler:      %-24s ║\n", 
                     cooler.estado ? "LIGADO" : "DESLIGADO");
        Serial.printf("║ Heater:      %-24s ║\n", 
                     heater.estado ? "LIGADO" : "DESLIGADO");
        Serial.println(F("╚══════════════════════════════════════╝\n"));
    }
}

// ========================================
// STATUS DETALHADO PARA FRONTEND
// ========================================

DetailedControlStatus getDetailedStatus() {
    DetailedControlStatus status;
    
    // Estado dos atuadores
    status.coolerActive = cooler.estado;
    status.heaterActive = heater.estado;
    
    // Pico estimado
    status.estimatedPeak = cv.estimatedPeak;
    status.peakDetection = (doPosPeakDetect || doNegPeakDetect);
    
    // Inicializa espera
    status.isWaiting = false;
    status.waitTimeRemaining = 0;
    status.waitReason = "";
    
    // Estado da máquina e tempos de espera
    uint16_t sinceHeating = timeSince(lastHeatTime);
    uint16_t sinceCooling = timeSince(lastCoolTime);
    uint16_t sinceIdle = timeSince(lastIdleTime);
    
    switch (controlState) {
        case STATE_OFF:
            status.stateName = "DESLIGADO";
            break;
            
        case IDLE:
            status.stateName = "IDLE";
            break;
            
        case COOLING:
            status.stateName = "RESFRIANDO";
            break;
            
        case HEATING:
            status.stateName = "AQUECENDO";
            break;
            
        case WAITING_TO_COOL:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitTimeRemaining = waitTime;
            
            // Determina o motivo da espera
            if (sinceHeating < cc.mutexDeadTime) {
                status.waitReason = "Proteção: mudança aquecimento→resfriamento";
                status.waitTimeRemaining = cc.mutexDeadTime - sinceHeating;
            } else if (sinceCooling < cc.minCoolIdleTime) {
                status.waitReason = "Proteção: intervalo mínimo compressor";
                status.waitTimeRemaining = cc.minCoolIdleTime - sinceCooling;
            } else {
                status.waitReason = "Aguardando para resfriar";
            }
            break;
            
        case WAITING_TO_HEAT:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitTimeRemaining = waitTime;
            
            if (sinceCooling < cc.mutexDeadTime) {
                status.waitReason = "Proteção: mudança resfriamento→aquecimento";
                status.waitTimeRemaining = cc.mutexDeadTime - sinceCooling;
            } else if (sinceHeating < cc.minHeatIdleTime) {
                status.waitReason = "Proteção: intervalo mínimo heater";
                status.waitTimeRemaining = cc.minHeatIdleTime - sinceHeating;
            } else {
                status.waitReason = "Aguardando para aquecer";
            }
            break;
            
        case WAITING_FOR_PEAK_DETECT:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitReason = "Aguardando estabilização (detecção de pico)";
            status.peakDetection = true;
            status.waitTimeRemaining = 0;
            break;
            
        case COOLING_MIN_TIME:
            status.stateName = "RESFRIANDO";
            status.isWaiting = true;
            status.waitReason = "Tempo mínimo de resfriamento";
            
            if (sinceIdle < cc.minCoolTime) {
                status.waitTimeRemaining = cc.minCoolTime - sinceIdle;
            }
            break;
            
        case HEATING_MIN_TIME:
            status.stateName = "AQUECENDO";
            status.isWaiting = true;
            status.waitReason = "Tempo mínimo de aquecimento";
            
            if (sinceIdle < cc.minHeatTime) {
                status.waitTimeRemaining = cc.minHeatTime - sinceIdle;
            }
            break;
            
        default:
            status.stateName = "DESCONHECIDO";
            break;
    }
    
    return status;
}

// ========================================
// FUNÇÃO DE RESET (compatibilidade)
// ========================================

void resetPIDState() {
    resetBrewPiControl();
    
    // Desliga tudo
    cooler.estado = false;
    heater.estado = false;
    cooler.atualizar();
    heater.atualizar();
    
    controlInitialized = false;
    
    Serial.println(F("[BrewPi] 🔄 Sistema de controle resetado completamente"));
}