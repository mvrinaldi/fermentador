// rampa_suave.cpp
#include "rampa_suave.h"
#include "definitions.h"
#include "controle_temperatura.h"
#include "controle_fermentacao.h"

static SmoothRampState rampState;

void setupSmoothRamp(float startTemp, float endTemp) {
    // ✅ PROTEÇÃO: Se já tem rampa ativa para o mesmo destino, não recria!
    if (rampState.active && fabs(rampState.endTemp - endTemp) < 0.1f) {
        Serial.printf("[Rampa] ⚠️  Rampa já ativa para %.1f°C, ignorando...\n", endTemp);
        return;
    }
    
    // ✅ LOG DETALHADO
    Serial.println(F("\n╔════════════════════════════════════╗"));
    Serial.println(F("║     CONFIGURANDO RAMPA SUAVE       ║"));
    Serial.println(F("╠════════════════════════════════════╣"));
    Serial.printf("║ Início:   %6.1f°C                 ║\n", startTemp);
    Serial.printf("║ Destino:  %6.1f°C                 ║\n", endTemp);
    Serial.printf("║ Diferença: %5.1f°C                 ║\n", fabs(endTemp - startTemp));
    
    rampState.active = true;
    rampState.startTemp = startTemp;
    rampState.endTemp = endTemp;
    rampState.startTime = millis();
    
    float diff = endTemp - startTemp;
    float rampTimeHours = fabs(diff) / RAMP_RATE;
    
    Serial.printf("║ Taxa:      %5.2f°C/h              ║\n", RAMP_RATE);
    Serial.printf("║ Duração:   %5.1f horas            ║\n", rampTimeHours);
    Serial.println(F("╚════════════════════════════════════╝\n"));
    
    // Define temperatura inicial (PID começa de onde está)
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
        Serial.println(F("\n╔════════════════════════════════════╗"));
        Serial.println(F("║       RAMPA CONCLUÍDA!             ║"));
        Serial.println(F("╠════════════════════════════════════╣"));
        Serial.printf("║ Temperatura final: %6.1f°C        ║\n", rampState.endTemp);
        Serial.printf("║ Tempo decorrido:   %6.1f h        ║\n", elapsedHours);
        Serial.println(F("╚════════════════════════════════════╝\n"));
        
        rampState.active = false;
        updateTargetTemperature(rampState.endTemp);
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
            Serial.printf("[Rampa] Progresso: %.1f°C → %.1f°C (%.0f%% | %.1fh/%.1fh)\n", 
                         currentTarget, rampState.endTemp, 
                         progress * 100.0f, elapsedHours, totalRampHours);
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

// ✅ NOVA FUNÇÃO: Cancela rampa ativa
void cancelSmoothRamp() {
    if (!rampState.active) return;
    
    Serial.println(F("\n╔════════════════════════════════════╗"));
    Serial.println(F("║      ❌ RAMPA CANCELADA!           ║"));
    Serial.println(F("╠════════════════════════════════════╣"));
    Serial.printf("║ Estava em:  %6.1f°C               ║\n", fermentacaoState.tempTarget);
    Serial.printf("║ Indo para:  %6.1f°C               ║\n", rampState.endTemp);
    Serial.printf("║ Cancelada após: %.1f horas         ║\n", 
                 (millis() - rampState.startTime) / 3600000.0f);
    Serial.println(F("╚════════════════════════════════════╝\n"));
    
    rampState.active = false;
}

// ✅ NOVA FUNÇÃO: Debug da rampa
void debugSmoothRamp() {
    if (!rampState.active) {
        Serial.println(F("[Rampa] Nenhuma rampa ativa"));
        return;
    }
    
    unsigned long now = millis();
    unsigned long elapsedMillis = now - rampState.startTime;
    float elapsedHours = elapsedMillis / 3600000.0f;
    float totalRampHours = fabs(rampState.endTemp - rampState.startTemp) / RAMP_RATE;
    float progress = elapsedHours / totalRampHours;
    if (progress > 1.0f) progress = 1.0f;
    
    Serial.println(F("\n╔════════════════════════════════════╗"));
    Serial.println(F("║       DEBUG RAMPA SUAVE            ║"));
    Serial.println(F("╠════════════════════════════════════╣"));
    Serial.printf("║ Status:    ATIVA                   ║\n");
    Serial.printf("║ Início:    %6.1f°C                 ║\n", rampState.startTemp);
    Serial.printf("║ Destino:   %6.1f°C                 ║\n", rampState.endTemp);
    Serial.printf("║ Atual:     %6.1f°C                 ║\n", fermentacaoState.tempTarget);
    Serial.printf("║ Progresso: %5.1f%%                  ║\n", progress * 100.0f);
    Serial.printf("║ Tempo:     %5.1f / %.1f horas      ║\n", elapsedHours, totalRampHours);
    Serial.println(F("╚════════════════════════════════════╝\n"));
}