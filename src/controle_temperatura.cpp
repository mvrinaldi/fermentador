#include <Arduino.h>
#include <DallasTemperature.h>
#include "globais.h"
#include "estruturas.h"
#include "definitions.h"

// Parâmetros PID e Variáveis de Controle
static float integral = 0;
static float lastError = 0;
static float pidOutput = 0;
static unsigned long lastPIDTime = 0;
static const float KP = 20.0, KI = 0.5, KD = 5.0;
static const float DEADBAND = 0.3;

static unsigned long lastCoolerOff = 0, lastHeaterOff = 0;
static unsigned long lastCoolerOn = 0, lastHeaterOn = 0;
const unsigned long MIN_CYCLE_TIME = 900000; // 15 min

void controle_temperatura() {
    unsigned long now = millis();

    if (now - lastPIDTime < 5000) return;
    lastPIDTime = now;

    if (!fermentacaoState.active) {
        cooler.estado = false;
        heater.estado = false;
        cooler.atualizar();
        heater.atualizar();
        return;
    }

    // Solicita temperaturas ao barramento global
    sensors.requestTemperatures();
    float tempFerm = sensors.getTempCByIndex(0); 
    float tempGel = sensors.getTempCByIndex(1);
    
    if (tempFerm == DEVICE_DISCONNECTED_C || tempFerm < -20) return;
    state.currentTemp = tempFerm; 

    float setpoint = fermentacaoState.tempTarget;
    float error = setpoint - tempFerm;
    if (abs(error) < DEADBAND) error = 0;

    float dt = 5.0; 
    integral = constrain(integral + (error * dt), -100, 100);
    float dTerm = (error - lastError) / dt;
    lastError = error;

    pidOutput = constrain((KP * error) + (KI * integral) + (KD * dTerm), -100, 100);

    // Lógica de Acionamento
    if (pidOutput < -2.0) { // RESFRIAR
        heater.estado = false;
        if (!cooler.estado && (now - lastCoolerOff > MIN_CYCLE_TIME) && (now - lastHeaterOff > 60000)) {
            cooler.estado = true;
            lastCoolerOn = now;
        }
    } 
    else if (pidOutput > 2.0) { // AQUECER
        cooler.estado = false;
        if (!heater.estado && (now - lastHeaterOff > 300000) && (now - lastCoolerOff > 60000)) {
            heater.estado = true;
            lastHeaterOn = now;
        }
    } 
    else { // NEUTRO
        if (cooler.estado && (now - lastCoolerOn > 180000)) {
            cooler.estado = false;
            lastCoolerOff = now;
        }
        if (heater.estado && (now - lastHeaterOn > 120000)) {
            heater.estado = false;
            lastHeaterOff = now;
        }
    }

    cooler.atualizar();
    heater.atualizar();

    // Uso da variável tempGel para eliminar o Warning e monitorar a geladeira
    Serial.printf("Ferm: %.2fC | Gel: %.2fC | Alvo: %.2fC | PID: %.2f\n", 
                  tempFerm, tempGel, setpoint, pidOutput);
}