// BrewPiTicks.h - Sistema de gerenciamento de tempo
#pragma once

#include <Arduino.h>

typedef uint32_t ticks_millis_t;
typedef uint32_t ticks_micros_t;
typedef uint16_t ticks_seconds_t;

// ========================================
// CLASSE DE TICKS
// ========================================

class BrewPiTicks {
public:
    BrewPiTicks() {}
    
    // Retorna milissegundos atuais
    ticks_millis_t millis() const {
        return ::millis();
    }
    
    // Retorna microssegundos atuais
    ticks_micros_t micros() const {
        return ::micros();
    }
    
    // Retorna segundos atuais (com overflow a cada ~18.2 horas)
    ticks_seconds_t seconds() const {
        return ::millis() / 1000;
    }
    
    // Calcula tempo decorrido desde timestamp (trata overflow)
    ticks_seconds_t timeSince(ticks_seconds_t previousTime) const {
        ticks_seconds_t currentTime = seconds();
        
        if (currentTime >= previousTime) {
            return currentTime - previousTime;
        } else {
            // Overflow ocorreu
            // Adiciona 1 dia (86400s) para cálculo
            return (currentTime + 86400) - (previousTime + 86400);
        }
    }
};

// ========================================
// CLASSE DE DELAY
// ========================================

class BrewPiDelay {
public:
    void millis(uint16_t milliseconds) {
        ::delay(milliseconds);
    }
    
    void seconds(uint16_t seconds) {
        millis(seconds * 1000);
    }
    
    void microseconds(uint32_t microseconds) {
        ::delayMicroseconds(microseconds);
    }
};

// Instâncias globais
extern BrewPiTicks ticks;
extern BrewPiDelay wait;
