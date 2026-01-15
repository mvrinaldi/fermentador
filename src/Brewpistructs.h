// BrewPiStructs.h - Estruturas de dados do BrewPi adaptadas
#pragma once

#include <Arduino.h>
#include "definitions.h"

// ========================================
// FORMATO DE TEMPERATURA FIXED-POINT
// ========================================
// BrewPi usa formato fixed-point com 9 bits fracionários
// Permite precisão de 1/512 = 0.00195°C
// Range: -128°C a +127.998°C

#define TEMP_FIXED_POINT_BITS 9
#define TEMP_FIXED_POINT_SCALE (1L << TEMP_FIXED_POINT_BITS)  // 512
#define C_OFFSET (-16384)  // Offset para -32°C

typedef int16_t temperature;
typedef int32_t long_temperature;

// Temperatura inválida
#define INVALID_TEMP -32768

// ========================================
// FUNÇÕES DE CONVERSÃO
// ========================================

// Converte float para fixed-point
inline temperature floatToTemp(float tempFloat) {
    return (temperature)((tempFloat * TEMP_FIXED_POINT_SCALE) + C_OFFSET);
}

// Converte fixed-point para float
inline float tempToFloat(temperature temp) {
    if (temp == INVALID_TEMP) return -127.0f;
    return ((float)(temp - C_OFFSET)) / TEMP_FIXED_POINT_SCALE;
}

// Converte inteiro para fixed-point
inline temperature intToTemp(int tempInt) {
    return ((temperature)tempInt << TEMP_FIXED_POINT_BITS) + C_OFFSET;
}

// Converte inteiro para diferença de temperatura
inline temperature intToTempDiff(int diff) {
    return ((temperature)diff << TEMP_FIXED_POINT_BITS);
}

// Multiplica temperatura por fator (fator também em fixed-point)
inline temperature multiplyFactorTemperatureDiff(temperature factor, temperature diff) {
    return (temperature)(((long_temperature)factor * diff) >> TEMP_FIXED_POINT_BITS);
}

// Versão para long_temperature
inline temperature multiplyFactorTemperatureDiffLong(temperature factor, long_temperature diff) {
    return (temperature)((factor * diff) >> TEMP_FIXED_POINT_BITS);
}

// Constrain para temperatura
inline temperature constrainTemp(long_temperature val, temperature min, temperature max) {
    if (val < min) return min;
    if (val > max) return max;
    return (temperature)val;
}

// Converte long para temperature com saturação
inline temperature constrainTemp16(long_temperature val) {
    if (val > 32767) return 32767;
    if (val < -32768) return -32768;
    return (temperature)val;
}

// ========================================
// MODOS DE CONTROLE
// ========================================

#define MODE_OFF 'o'
#define MODE_FRIDGE_CONSTANT 'f'
#define MODE_BEER_CONSTANT 'b'
#define MODE_BEER_PROFILE 'p'
#define MODE_TEST 't'

// ========================================
// ESTADOS DA MÁQUINA
// ========================================

enum BrewPiState {
    IDLE = 0,
    STATE_OFF,
    DOOR_OPEN,
    HEATING,
    COOLING,
    WAITING_TO_COOL,
    WAITING_TO_HEAT,
    WAITING_FOR_PEAK_DETECT,
    COOLING_MIN_TIME,
    HEATING_MIN_TIME,
    NUM_STATES
};

// ========================================
// CONSTANTES DE CONTROLE
// ========================================

struct ControlConstants {
    // Formato de temperatura
    char tempFormat;  // 'C' ou 'F'
    
    // Limites de temperatura
    temperature tempSettingMin;  // Mínimo: 1°C
    temperature tempSettingMax;  // Máximo: 30°C
    
    // Parâmetros PID (em fixed-point)
    temperature Kp;              // Proporcional: 5.0
    temperature Ki;              // Integral: 0.25
    temperature Kd;              // Derivativo: -1.5
    temperature iMaxError;       // Erro máximo integral: 0.5°C
    
    // Faixas IDLE (em fixed-point)
    temperature idleRangeHigh;   // +1.0°C
    temperature idleRangeLow;    // -1.0°C
    
    // Alvos de detecção de pico (em fixed-point)
    temperature heatingTargetUpper;   // +0.3°C
    temperature heatingTargetLower;   // -0.2°C
    temperature coolingTargetUpper;   // +0.2°C
    temperature coolingTargetLower;   // -0.3°C
    
    // Tempos de proteção (segundos)
    uint16_t maxHeatTimeForEstimate;  // 600s (10min)
    uint16_t maxCoolTimeForEstimate;  // 1200s (20min)
    
    // Coeficientes de filtro
    uint8_t fridgeFastFilter;    // 1
    uint8_t fridgeSlowFilter;    // 4
    uint8_t fridgeSlopeFilter;   // 3
    uint8_t beerFastFilter;      // 3
    uint8_t beerSlowFilter;      // 4
    uint8_t beerSlopeFilter;     // 4
    
    // Flags
    uint8_t lightAsHeater;       // 0 = não usa luz como aquecedor
    uint8_t rotaryHalfSteps;     // 0 = passos completos
    
    // Limite PID
    temperature pidMax;          // ±10°C
    
    // Tempos mínimos de ciclo
    uint16_t minCoolTime;        // 600s (10min)
    uint16_t minCoolIdleTime;    // 900s (15min)
    uint16_t minHeatTime;        // 300s (5min)
    uint16_t minHeatIdleTime;    // 600s (10min)
    uint16_t mutexDeadTime;      // 900s (15min)
};

// ========================================
// VARIÁVEIS DE CONTROLE
// ========================================

struct ControlVariables {
    temperature beerDiff;           // Erro atual (setpoint - temperatura)
    long_temperature diffIntegral;  // Acumulador integral
    temperature beerSlope;          // Taxa de mudança (slope)
    temperature p;                  // Componente Proporcional
    temperature i;                  // Componente Integral
    temperature d;                  // Componente Derivativo
    temperature estimatedPeak;      // Pico estimado atual
    temperature negPeakEstimate;    // Última estimativa de pico negativo
    temperature posPeakEstimate;    // Última estimativa de pico positivo
    temperature negPeak;            // Último pico negativo detectado
    temperature posPeak;            // Último pico positivo detectado
};

// ========================================
// CONFIGURAÇÕES DE CONTROLE
// ========================================

struct ControlSettings {
    char mode;                   // Modo de controle
    temperature beerSetting;     // Setpoint da cerveja
    temperature fridgeSetting;   // Setpoint da geladeira (calculado pelo PID)
    temperature heatEstimator;   // Estimador de overshoot no aquecimento (°C/h * 256)
    temperature coolEstimator;   // Estimador de overshoot no resfriamento (°C/h * 256)
};

// ========================================
// CONSTANTES PADRÃO
// ========================================

const ControlConstants DEFAULT_CONTROL_CONSTANTS = {
    /* tempFormat */ 'C',
    /* tempSettingMin */ intToTemp(1),     // +1°C
    /* tempSettingMax */ intToTemp(30),    // +30°C
    
    /* Kp */ intToTempDiff(5),                        // +5.0
    /* Ki */ (temperature)(intToTempDiff(1)/4),       // +0.25
    /* Kd */ (temperature)(intToTempDiff(-3)/2),      // -1.5
    /* iMaxError */ (temperature)(intToTempDiff(5)/10), // 0.5°C
    
    /* idleRangeHigh */ intToTempDiff(1),  // +1.0°C
    /* idleRangeLow */ intToTempDiff(-1),  // -1.0°C
    
    /* heatingTargetUpper */ (temperature)(intToTempDiff(3)/10),  // +0.3°C
    /* heatingTargetLower */ (temperature)(intToTempDiff(-2)/10), // -0.2°C
    /* coolingTargetUpper */ (temperature)(intToTempDiff(2)/10),  // +0.2°C
    /* coolingTargetLower */ (temperature)(intToTempDiff(-3)/10), // -0.3°C
    
    /* maxHeatTimeForEstimate */ 600,
    /* maxCoolTimeForEstimate */ 1200,
    
    /* fridgeFastFilter */ 1,
    /* fridgeSlowFilter */ 4,
    /* fridgeSlopeFilter */ 3,
    /* beerFastFilter */ 3,
    /* beerSlowFilter */ 4,
    /* beerSlopeFilter */ 4,
    
    /* lightAsHeater */ 0,
    /* rotaryHalfSteps */ 0,
    
    /* pidMax */ intToTempDiff(10),  // ±10°C
    
    /* minCoolTime */ 600,        // 10min
    /* minCoolIdleTime */ 900,    // 15min
    /* minHeatTime */ 300,        // 5min
    /* minHeatIdleTime */ 600,    // 10min
    /* mutexDeadTime */ 900       // 15min
};

// ========================================
// CONFIGURAÇÕES PADRÃO
// ========================================

const ControlSettings DEFAULT_CONTROL_SETTINGS = {
    /* mode */ MODE_BEER_CONSTANT,
    /* beerSetting */ intToTemp(20),   // 20°C
    /* fridgeSetting */ intToTemp(20), // 20°C
    /* heatEstimator */ (temperature)(intToTempDiff(2)/10),  // 0.2°C/h
    /* coolEstimator */ intToTempDiff(5)      // 5.0°C/h
};

// ========================================
// TEMPOS DE DETECÇÃO DE PICO
// ========================================

#define COOL_PEAK_DETECT_TIME 1800  // 30 minutos
#define HEAT_PEAK_DETECT_TIME 900   // 15 minutos