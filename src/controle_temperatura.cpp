// controle_temperatura.cpp - Controle PID local (independente de rede)
#include <Arduino.h>
#include <DallasTemperature.h>
#include "globais.h"
#include "estruturas.h"
#include "definitions.h"
#include "http_client.h"

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
static bool firstRun = true;

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

    // ✅ Inicialização para primeira execução
    if (firstRun) {
        firstRun = false;
        
        // Inicializa com 0 para permitir acionamento imediato na primeira execução
        lastCoolerOff = 0;
        lastHeaterOff = 0;
        lastCoolerOn = 0;
        lastHeaterOn = 0;
        
        Serial.println(F("[PID] ✅ Sistema de controle inicializado"));
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
// LÓGICA DE ACIONAMENTO DOS RELÉS - MÉTODO DA SUBTRAÇÃO
// ========================================

// RESFRIAMENTO: PID negativo significa temp acima do setpoint
if (pidOutput < -2.0) {
    // Desliga heater se estiver ligado
    if (heater.estado) {
        heater.estado = false;
        heater.atualizar();
        lastHeaterOff = now;
        Serial.println(F("[PID] 🔥 Heater desligado para iniciar resfriamento"));
    }
    
    // Verificações usando subtração (protege contra overflow)
    // Se lastCoolerOff == 0, significa primeira execução, permite ligar
    bool coolerCycleOk = (lastCoolerOff == 0) || (now - lastCoolerOff >= MIN_COOLER_CYCLE);
    bool heaterDelayOk = (lastHeaterOff == 0) || (now - lastHeaterOff >= MIN_DELAY_BETWEEN_RELAYS);
    
    // DEBUG
    Serial.println(F("\n[PID DEBUG - RESFRIAMENTO] ======================"));
    Serial.printf("now = %lu\n", now);
    Serial.printf("lastCoolerOff = %lu (diff=%lu, MIN_COOLER_CYCLE=%lu)\n", 
                  lastCoolerOff, now - lastCoolerOff, MIN_COOLER_CYCLE);
    Serial.printf("lastHeaterOff = %lu (diff=%lu, MIN_DELAY_BETWEEN_RELAYS=%lu)\n", 
                  lastHeaterOff, now - lastHeaterOff, MIN_DELAY_BETWEEN_RELAYS);
    Serial.printf("coolerCycleOk = %s\n", coolerCycleOk ? "TRUE" : "FALSE");
    Serial.printf("heaterDelayOk = %s\n", heaterDelayOk ? "TRUE" : "FALSE");
    Serial.printf("cooler.estado = %d\n", cooler.estado);
    Serial.println(F("=================================================\n"));
    
    if (!cooler.estado && coolerCycleOk && heaterDelayOk) {
        cooler.estado = true;
        lastCoolerOn = now;
        cooler.atualizar();
        Serial.printf("[PID] ❄️  Cooler LIGADO (erro: %.2f°C, PID: %.2f)\n", error, pidOutput);
    } else if (!cooler.estado) {
        Serial.println(F("[PID] ⚠️  Cooler NÃO ligou. Razões:"));
        if (cooler.estado) {
            Serial.println(F("  - Cooler já está ligado"));
        }
        if (!coolerCycleOk) {
            Serial.printf("  - Cooler cycle: precisa esperar %lu ms mais\n", 
                         MIN_COOLER_CYCLE - (now - lastCoolerOff));
        }
        if (!heaterDelayOk) {
            Serial.printf("  - Heater delay: precisa esperar %lu ms mais\n", 
                         MIN_DELAY_BETWEEN_RELAYS - (now - lastHeaterOff));
        }
    }
} 
// AQUECIMENTO
else if (pidOutput > 2.0) {
    // Desliga cooler se estiver ligado
    if (cooler.estado) {
        cooler.estado = false;
        cooler.atualizar();
        lastCoolerOff = now;
        Serial.println(F("[PID] ❄️  Cooler desligado para iniciar aquecimento"));
    }
    
    // Verificações usando subtração (protege contra overflow)
    // Se lastHeaterOff == 0, significa primeira execução, permite ligar
    bool heaterCycleOk = (lastHeaterOff == 0) || (now - lastHeaterOff >= MIN_HEATER_CYCLE);
    bool coolerDelayOk = (lastCoolerOff == 0) || (now - lastCoolerOff >= MIN_DELAY_BETWEEN_RELAYS);
    
    // DEBUG
    Serial.println(F("\n[PID DEBUG - AQUECIMENTO] ======================="));
    Serial.printf("now = %lu\n", now);
    Serial.printf("lastHeaterOff = %lu (diff=%lu, MIN_HEATER_CYCLE=%lu)\n", 
                  lastHeaterOff, now - lastHeaterOff, MIN_HEATER_CYCLE);
    Serial.printf("lastCoolerOff = %lu (diff=%lu, MIN_DELAY_BETWEEN_RELAYS=%lu)\n", 
                  lastCoolerOff, now - lastCoolerOff, MIN_DELAY_BETWEEN_RELAYS);
    Serial.printf("heaterCycleOk = %s\n", heaterCycleOk ? "TRUE" : "FALSE");
    Serial.printf("coolerDelayOk = %s\n", coolerDelayOk ? "TRUE" : "FALSE");
    Serial.printf("heater.estado = %d\n", heater.estado);
    Serial.println(F("=================================================\n"));
    
    if (!heater.estado && heaterCycleOk && coolerDelayOk) {
        heater.estado = true;
        lastHeaterOn = now;
        heater.atualizar();
        Serial.printf("[PID] 🔥 Heater LIGADO (erro: %.2f°C, PID: %.2f)\n", error, pidOutput);
    } else if (!heater.estado) {
        Serial.println(F("[PID] ⚠️  Heater NÃO ligou. Razões:"));
        if (!heaterCycleOk) {
            Serial.printf("  - Heater cycle: precisa esperar %lu ms mais\n", 
                         MIN_HEATER_CYCLE - (now - lastHeaterOff));
        }
        if (!coolerDelayOk) {
            Serial.printf("  - Cooler delay: precisa esperar %lu ms mais\n", 
                         MIN_DELAY_BETWEEN_RELAYS - (now - lastCoolerOff));
        }
    }
}
// ZONA NEUTRA
else {
    // Desliga cooler após tempo mínimo ligado
    if (cooler.estado && (now - lastCoolerOn >= MIN_COOLER_ON)) {
        cooler.estado = false;
        cooler.atualizar();
        lastCoolerOff = now;
        Serial.println(F("[PID] ❄️  Cooler DESLIGADO (temperatura OK)"));
    }
    
    // Desliga heater após tempo mínimo ligado
    if (heater.estado && (now - lastHeaterOn >= MIN_HEATER_ON)) {
        heater.estado = false;
        heater.atualizar();
        lastHeaterOff = now;
        Serial.println(F("[PID] 🔥 Heater DESLIGADO (temperatura OK)"));
    }
}

    // ========================================
    // LOG SERIAL (debug resumido)
    // ========================================
    static unsigned long lastLog = 0;
    if (now - lastLog >= 30000) {
        lastLog = now;
        
        Serial.println(F("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
        Serial.printf("🌡️  Fermentador: %.2f°C\n", tempFerm);
        Serial.printf("🌡️  Geladeira:   %.2f°C\n", tempGel);
        Serial.printf("🎯 Alvo:         %.2f°C\n", setpoint);
        Serial.printf("📊 Erro:         %.2f°C\n", error);
        Serial.printf("📊 PID Output:   %.2f\n", pidOutput);
        Serial.printf("❄️  Cooler:       %s", cooler.estado ? "LIGADO" : "DESLIGADO");
        if (cooler.estado) {
            Serial.printf(" (há %lu s)", (now - lastCoolerOn) / 1000);
        }
        Serial.println();
        Serial.printf("🔥 Heater:       %s", heater.estado ? "LIGADO" : "DESLIGADO");
        if (heater.estado) {
            Serial.printf(" (há %lu s)", (now - lastHeaterOn) / 1000);
        }
        Serial.println();
        Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
    }
}

// ========================================
// FUNÇÃO PARA RESETAR ESTADO
// ========================================
void resetPIDState() {
    integral = 0;
    lastError = 0;
    pidOutput = 0;
    firstRun = true;
    
    // Desliga tudo
    cooler.estado = false;
    heater.estado = false;
    cooler.atualizar();
    heater.atualizar();
    
    // Reset dos tempos
    lastCoolerOff = 0;
    lastHeaterOff = 0;
    lastCoolerOn = 0;
    lastHeaterOn = 0;
    
    Serial.println(F("[PID] 🔄 Estado do PID resetado"));
}