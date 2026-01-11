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
// ✅ CORREÇÃO: Inicializar com valores que permitam primeira execução
static unsigned long lastCoolerOff = 0;
static unsigned long lastHeaterOff = 0;
static unsigned long lastCoolerOn = 0;
static unsigned long lastHeaterOn = 0;
static bool firstRun = true;  // ← Flag para primeira execução

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

    // ✅ CORREÇÃO: Na primeira execução com fermentação ativa,
    // inicializa os tempos para permitir acionamento imediato se necessário
    if (firstRun) {
        firstRun = false;
        
        // Inicializa com um valor que garante que a subtração (now - last) 
        // resulte em algo maior que os delays.
        lastCoolerOff = now - MIN_COOLER_CYCLE - 1000; 
        lastHeaterOff = now - MIN_DELAY_BETWEEN_RELAYS - 1000;

        // Calcula o maior dos tempos para garantir que TODAS as condições passem
        unsigned long maxDelay = max(MIN_COOLER_CYCLE, MIN_DELAY_BETWEEN_RELAYS);
        maxDelay = max(maxDelay, MIN_HEATER_CYCLE);
        
        lastCoolerOff = now - maxDelay - 1000;  // Permite ligar imediatamente
        lastHeaterOff = now - maxDelay - 1000;  // Permite ligar imediatamente
        lastCoolerOn = now - MIN_COOLER_ON - 1000;
        lastHeaterOn = now - MIN_HEATER_ON - 1000;
        
        Serial.println(F("[PID] ✅ Sistema de controle inicializado"));
        Serial.printf("[PID] ℹ️  Todos os delays zerados (max: %lu ms)\n", maxDelay);
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
    // Se o heater estava ligado, agora ele desliga e registramos o tempo
    if (heater.estado == true) {
        heater.estado = false;
        lastHeaterOff = now; // Agora o delay de 60s começará a contar
        Serial.println(F("[PID] 🔥 Heater desligado para iniciar resfriamento"));
    }
    
// Verificações de segurança usando a técnica de subtração (protege contra overflow/underflow)
bool coolerCycleOk = (now - lastCoolerOff >= MIN_COOLER_CYCLE);
bool heaterDelayOk = (now - lastHeaterOff >= MIN_DELAY_BETWEEN_RELAYS);
    
    Serial.println(F("\n[PID DEBUG] ==================================="));
    Serial.printf("now = %lu\n", now);
    Serial.printf("lastCoolerOff = %lu\n", lastCoolerOff);
    Serial.printf("lastHeaterOff = %lu\n", lastHeaterOff);
    Serial.printf("MIN_COOLER_CYCLE = %lu\n", MIN_COOLER_CYCLE);
    Serial.printf("MIN_DELAY_BETWEEN_RELAYS = %lu\n", MIN_DELAY_BETWEEN_RELAYS);
    Serial.printf("lastCoolerOff + MIN_COOLER_CYCLE = %lu\n", lastCoolerOff + MIN_COOLER_CYCLE);
    Serial.printf("lastHeaterOff + MIN_DELAY_BETWEEN_RELAYS = %lu\n", lastHeaterOff + MIN_DELAY_BETWEEN_RELAYS);
    Serial.printf("coolerCycleOk = %s (now >= %lu)\n", coolerCycleOk ? "TRUE" : "FALSE", lastCoolerOff + MIN_COOLER_CYCLE);
    Serial.printf("heaterDelayOk = %s (now >= %lu)\n", heaterDelayOk ? "TRUE" : "FALSE", lastHeaterOff + MIN_DELAY_BETWEEN_RELAYS);
    Serial.printf("cooler.estado = %d\n", cooler.estado);
    Serial.printf("Condição final: !cooler.estado=%d && coolerCycleOk=%d && heaterDelayOk=%d\n", 
                  !cooler.estado, coolerCycleOk, heaterDelayOk);
    Serial.println(F("=======================================\n"));
    
    if (!cooler.estado && coolerCycleOk && heaterDelayOk) {
        cooler.estado = true;
        lastCoolerOn = now;
        cooler.atualizar();
        Serial.printf("[PID] ❄️  Cooler LIGADO (erro: %.2f°C, PID: %.2f)\n", error, pidOutput);
    } else {
        Serial.println(F("[PID] ⚠️  Cooler NÃO ligou. Razões:"));
        if (cooler.estado) {
            Serial.println(F("  - Cooler já está ligado"));
        }
        if (!coolerCycleOk) {
            Serial.printf("  - Cooler cycle: aguardando %lu ms\n", 
                         (lastCoolerOff + MIN_COOLER_CYCLE) - now);
        }
        if (!heaterDelayOk) {
            Serial.printf("  - Heater delay: aguardando %lu ms\n", 
                         (lastHeaterOff + MIN_DELAY_BETWEEN_RELAYS) - now);
        }
    }
} 
// AQUECIMENTO
else if (pidOutput > 2.0) {
    // Desliga cooler se estiver ligado
    if (cooler.estado) {
        cooler.estado = false;
        lastCoolerOff = now; // SÓ atualiza quando realmente desliga
    }
    
    // Comparação segura
    bool heaterCycleOk = (firstRun || (now >= lastHeaterOff + MIN_HEATER_CYCLE));
    bool coolerDelayOk = (firstRun || (now >= lastCoolerOff + MIN_DELAY_BETWEEN_RELAYS));
    
    if (!heater.estado && heaterCycleOk && coolerDelayOk) {
        heater.estado = true;
        lastHeaterOn = now;
        Serial.printf("[PID] 🔥 Heater LIGADO (erro: %.2f°C, PID: %.2f)\n", error, pidOutput);
    } else if (!heater.estado) {
        if (!heaterCycleOk) {
            Serial.printf("[PID] ⏳ Heater: aguardando %lu ms\n", 
                         (lastHeaterOff + MIN_HEATER_CYCLE) - now);
        }
        if (!coolerDelayOk) {
            Serial.printf("[PID] ⏳ Cooler delay: aguardando %lu ms\n", 
                         (lastCoolerOff + MIN_DELAY_BETWEEN_RELAYS) - now);
        }
    }
}
// ZONA NEUTRA
else {
    if (cooler.estado && (now >= lastCoolerOn + MIN_COOLER_ON)) {
        cooler.estado = false;
        lastCoolerOff = now;
        Serial.println(F("[PID] ❄️  Cooler DESLIGADO (temperatura OK)"));
    }
    
    if (heater.estado && (now >= lastHeaterOn + MIN_HEATER_ON)) {
        heater.estado = false;
        lastHeaterOff = now;
        Serial.println(F("[PID] 🔥 Heater DESLIGADO (temperatura OK)"));
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
// FUNÇÃO PARA RESETAR ESTADO (chamar quando desativar fermentação)
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
    
    Serial.println(F("[PID] 🔄 Estado do PID resetado"));
}