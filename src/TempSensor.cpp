// TempSensor.cpp - Implementação do sensor com filtros
#include "TempSensor.h"

TempSensor::TempSensor(DallasTemperature* sens, uint8_t idx)
    : sensors(sens)
    , sensorIndex(idx)
    , connected(false)
    , currentTemp(INVALID_TEMP)
    , fastFiltered(INVALID_TEMP)
    , slowFiltered(INVALID_TEMP)
    , slope(0)
    , fastFilterCoeff(1)
    , slowFilterCoeff(4)
    , slopeFilterCoeff(3)
    , prevSlowFiltered(INVALID_TEMP)
    , lastSlopeUpdate(0)
    , posPeakDetected(false)
    , negPeakDetected(false)
    , posPeak(INVALID_TEMP)
    , negPeak(INVALID_TEMP)
    , prevTemp(INVALID_TEMP)
{
}

void TempSensor::init() {
    // Tenta ler temperatura
    update();
    
    if (connected) {
        // Inicializa filtros com valor atual
        fastFiltered = currentTemp;
        slowFiltered = currentTemp;
        prevSlowFiltered = currentTemp;
        prevTemp = currentTemp;
    }
}

void TempSensor::update() {
    // Lê temperatura do sensor Dallas
    float tempC = sensors->getTempCByIndex(sensorIndex);
    
    // Verifica se leitura é válida
    if (tempC == DEVICE_DISCONNECTED_C || tempC < -20.0f || tempC > 50.0f) {
        connected = false;
        currentTemp = INVALID_TEMP;
        return;
    }
    
    connected = true;
    currentTemp = floatToTemp(tempC);
    
    // Aplica filtros
    if (fastFiltered == INVALID_TEMP) {
        // Primeira leitura - inicializa filtros
        fastFiltered = currentTemp;
        slowFiltered = currentTemp;
        prevSlowFiltered = currentTemp;
        prevTemp = currentTemp;
    } else {
        // Aplica filtros exponenciais
        fastFiltered = applyFilter(currentTemp, fastFiltered, fastFilterCoeff);
        slowFiltered = applyFilter(currentTemp, slowFiltered, slowFilterCoeff);
    }
    
    // Atualiza slope a cada 10 segundos
    ticks_seconds_t now = ticks.seconds();
    if (now - lastSlopeUpdate >= 10) {
        temperature diff = slowFiltered - prevSlowFiltered;
        uint16_t timeDiff = now - lastSlopeUpdate;
        
        if (timeDiff > 0) {
            // Slope em unidades de temperatura por segundo, depois converte para temperatura por hora
            long_temperature slopePerSecond = ((long_temperature)diff * 3600) / timeDiff;
            
            // Aplica filtro no slope
            temperature newSlope = constrainTemp16(slopePerSecond);
            slope = applyFilter(newSlope, slope, slopeFilterCoeff);
        }
        
        prevSlowFiltered = slowFiltered;
        lastSlopeUpdate = now;
    }
    
    // Detecção de picos
    if (prevTemp != INVALID_TEMP && currentTemp != INVALID_TEMP) {
        // Detecta pico positivo (máximo local)
        if (!posPeakDetected && prevTemp < fastFiltered && fastFiltered >= currentTemp) {
            posPeak = fastFiltered;
            posPeakDetected = true;
        }
        
        // Detecta pico negativo (mínimo local)
        if (!negPeakDetected && prevTemp > fastFiltered && fastFiltered <= currentTemp) {
            negPeak = fastFiltered;
            negPeakDetected = true;
        }
    }
    
    prevTemp = fastFiltered;
}

temperature TempSensor::applyFilter(temperature input, temperature prevOutput, uint8_t coeff) {
    if (prevOutput == INVALID_TEMP || input == INVALID_TEMP) {
        return input;
    }
    
    // Filtro exponencial: output = prevOutput + (input - prevOutput) * alpha
    // onde alpha = 1 / (2^coeff)
    // Implementação otimizada: output = prevOutput + (input - prevOutput) >> coeff
    
    long_temperature diff = (long_temperature)input - (long_temperature)prevOutput;
    diff = diff >> coeff;  // Divide por 2^coeff
    
    return constrainTemp16((long_temperature)prevOutput + diff);
}

void TempSensor::setFastFilterCoefficients(uint8_t coeff) {
    fastFilterCoeff = coeff;
}

void TempSensor::setSlowFilterCoefficients(uint8_t coeff) {
    slowFilterCoeff = coeff;
}

void TempSensor::setSlopeFilterCoefficients(uint8_t coeff) {
    slopeFilterCoeff = coeff;
}

temperature TempSensor::detectPosPeak() {
    if (posPeakDetected) {
        posPeakDetected = false;  // Reset flag
        return posPeak;
    }
    return INVALID_TEMP;
}

temperature TempSensor::detectNegPeak() {
    if (negPeakDetected) {
        negPeakDetected = false;  // Reset flag
        return negPeak;
    }
    return INVALID_TEMP;
}