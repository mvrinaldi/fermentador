// rampa_suave.cpp
#include "rampa_suave.h"
#include "definitions.h"
#include "controle_temperatura.h"
#include "controle_fermentacao.h"

static SmoothRampState rampState;

void setupSmoothRamp(float startTemp, float endTemp) {
    rampState.active = true;
    rampState.startTemp = startTemp;
    rampState.endTemp = endTemp;
    rampState.startTime = millis();
    
    float diff = endTemp - startTemp;
    float rampTimeHours = fabs(diff) / RAMP_RATE;
    
    Serial.printf("[Rampa] 🔄 Configurada: %.1f°C → %.1f°C em %.1f horas (%.1f°C/min)\n",
                 startTemp, endTemp, rampTimeHours, RAMP_RATE);
    
    // Define temperatura inicial
    updateTargetTemperature(startTemp);
}

void updateSmoothRamp() {
    if (!rampState.active) return;
    
    unsigned long now = millis();
    unsigned long elapsedMillis = now - rampState.startTime;
    float elapsedHours = elapsedMillis / 3600000.0f;
    
    // Tempo total estimado para rampa
    float totalRampHours = fabs(rampState.endTemp - rampState.startTemp) / RAMP_RATE;
    
    if (elapsedHours >= totalRampHours) {
        // Rampa concluída
        rampState.active = false;
        updateTargetTemperature(rampState.endTemp);
        Serial.printf("[Rampa] ✅ Concluída: %.1f°C alcançado\n", rampState.endTemp);
    } else {
        // Calcula temperatura intermediária
        float progress = elapsedHours / totalRampHours;
        if (progress > 1.0f) progress = 1.0f;
        
        float currentTarget = rampState.startTemp + 
                             (rampState.endTemp - rampState.startTemp) * progress;
        
        updateTargetTemperature(currentTarget);
        
        // Log periódico (a cada minuto)
        static unsigned long lastLog = 0;
        if (now - lastLog > 60000) {
            lastLog = now;
            Serial.printf("[Rampa] Progresso: %.1f°C (%.0f%%)\n", 
                         currentTarget, progress * 100.0f);
        }
    }
}

bool isSmoothRampActive() {
    return rampState.active;
}

float getCurrentRampTarget() {
    if (!rampState.active) return 0.0f;
    
    unsigned long now = millis();
    unsigned long elapsedMillis = now - rampState.startTime;
    float elapsedHours = elapsedMillis / 3600000.0f;
    
    float totalRampHours = fabs(rampState.endTemp - rampState.startTemp) / RAMP_RATE;
    
    if (elapsedHours >= totalRampHours) {
        return rampState.endTemp;
    }
    
    float progress = elapsedHours / totalRampHours;
    if (progress > 1.0f) progress = 1.0f;
    
    return rampState.startTemp + (rampState.endTemp - rampState.startTemp) * progress;
}