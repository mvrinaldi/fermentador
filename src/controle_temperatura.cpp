// controle_temperatura.cpp - Controle PID local (independente de rede)
#include <Arduino.h>
#include <DallasTemperature.h>
#include "globais.h"
#include "estruturas.h"
#include "definitions.h"

// ========================================
// PARÂMETROS PID
// ========================================
static const float KP = 20.0;  // Ganho proporcional
static const float KI = 0.5;   // Ganho integral
static const float KD = 5.0;   // Ganho derivativo
static const float DEADBAND = 0.3; // Banda morta (°C)

// ========================================
// VARIÁVEIS DE CONTROLE PID
// ========================================
static float integral = 0;
static float lastError = 0;
static float pidOutput = 0;
static unsigned long lastPIDTime = 0;

// ========================================
// TEMPOS DE CICLO DOS RELÉS
// ========================================
static unsigned long lastCoolerOff = 0;
static unsigned long lastHeaterOff = 0;
static unsigned long lastCoolerOn = 0;
static unsigned long lastHeaterOn = 0;

// As constantes de tempo já estão definidas em definitions.h:
// MIN_COOLER_CYCLE, MIN_HEATER_CYCLE, MIN_COOLER_ON, 
// MIN_HEATER_ON, MIN_DELAY_BETWEEN_RELAYS

// ========================================
// FUNÇÃO PRINCIPAL DE CONTROLE
// ========================================
void controle_temperatura() {
    unsigned long now = millis();

    // Executa PID a cada 5 segundos
    if (now - lastPIDTime < 5000) return;
    lastPIDTime = now;

    // Se não há fermentação ativa, desliga tudo
    if (!fermentacaoState.active) {
        cooler.estado = false;
        heater.estado = false;
        cooler.atualizar();
        heater.atualizar();
        return;
    }

    // ========================================
    // LEITURA DOS SENSORES
    // ========================================
    sensors.requestTemperatures();
    
    float tempFerm = sensors.getTempCByIndex(0); // Sensor do fermentador
    float tempGel = sensors.getTempCByIndex(1);  // Sensor da geladeira
    
    // Verifica se leitura é válida
    if (tempFerm == DEVICE_DISCONNECTED_C || tempFerm < -20 || tempFerm > 50) {
        Serial.println(F("[PID] ❌ Sensor fermentador com erro"));
        return; // Não controla sem sensor válido
    }
    
    // Atualiza temperatura atual do sistema
    state.currentTemp = tempFerm;

    // ========================================
    // CÁLCULO PID
    // ========================================
    float setpoint = fermentacaoState.tempTarget;
    float error = setpoint - tempFerm;
    
    // Banda morta: ignora erros pequenos
    if (abs(error) < DEADBAND) {
        error = 0;
    }

    float dt = 5.0; // Delta time em segundos
    
    // Termo integral (com anti-windup)
    integral = constrain(integral + (error * dt), -100, 100);
    
    // Termo derivativo
    float dTerm = (error - lastError) / dt;
    lastError = error;

    // Saída PID
    pidOutput = constrain((KP * error) + (KI * integral) + (KD * dTerm), -100, 100);

    // ========================================
    // LÓGICA DE ACIONAMENTO DOS RELÉS
    // ========================================
    
    // RESFRIAMENTO: PID negativo significa temp acima do setpoint
    if (pidOutput < -2.0) {
        // Precisa resfriar
        heater.estado = false; // Garante que heater está desligado
        
        // Só liga cooler se:
        // 1. Não está ligado ainda
        // 2. Passou tempo mínimo desde que desligou (15 min)
        // 3. Passou tempo mínimo desde que heater desligou (1 min)
        if (!cooler.estado && 
            (now - lastCoolerOff > MIN_COOLER_CYCLE) && 
            (now - lastHeaterOff > MIN_DELAY_BETWEEN_RELAYS)) {
            
            cooler.estado = true;
            lastCoolerOn = now;
            Serial.println(F("[PID] ❄️  Cooler LIGADO"));
        }
    } 
    // AQUECIMENTO: PID positivo significa temp abaixo do setpoint
    else if (pidOutput > 2.0) {
        // Precisa aquecer
        cooler.estado = false; // Garante que cooler está desligado
        
        // Só liga heater se:
        // 1. Não está ligado ainda
        // 2. Passou tempo mínimo desde que desligou (5 min)
        // 3. Passou tempo mínimo desde que cooler desligou (1 min)
        if (!heater.estado && 
            (now - lastHeaterOff > MIN_HEATER_CYCLE) && 
            (now - lastCoolerOff > MIN_DELAY_BETWEEN_RELAYS)) {
            
            heater.estado = true;
            lastHeaterOn = now;
            Serial.println(F("[PID] 🔥 Heater LIGADO"));
        }
    } 
    // ZONA NEUTRA: temperatura OK
    else {
        // Desliga cooler se passou tempo mínimo ligado (3 min)
        if (cooler.estado && (now - lastCoolerOn > MIN_COOLER_ON)) {
            cooler.estado = false;
            lastCoolerOff = now;
            Serial.println(F("[PID] ❄️  Cooler DESLIGADO"));
        }
        
        // Desliga heater se passou tempo mínimo ligado (2 min)
        if (heater.estado && (now - lastHeaterOn > MIN_HEATER_ON)) {
            heater.estado = false;
            lastHeaterOff = now;
            Serial.println(F("[PID] 🔥 Heater DESLIGADO"));
        }
    }

    // ========================================
    // ATUALIZA HARDWARE DOS RELÉS
    // ========================================
    cooler.atualizar();
    heater.atualizar();

    // ========================================
    // LOG SERIAL (debug)
    // ========================================
    static unsigned long lastLog = 0;
    if (now - lastLog >= 30000) { // Log a cada 30 segundos
        lastLog = now;
        
        Serial.println(F("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
        Serial.printf("🌡️  Fermentador: %.2f°C\n", tempFerm);
        Serial.printf("🌡️  Geladeira:   %.2f°C\n", tempGel);
        Serial.printf("🎯 Alvo:         %.2f°C\n", setpoint);
        Serial.printf("📊 PID Output:   %.2f\n", pidOutput);
        Serial.printf("❄️  Cooler:       %s\n", cooler.estado ? "LIGADO" : "DESLIGADO");
        Serial.printf("🔥 Heater:       %s\n", heater.estado ? "LIGADO" : "DESLIGADO");
        Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
    }
}