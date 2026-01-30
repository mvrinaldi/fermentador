// TempSensor.h - Sensor de temperatura com filtros adaptativos
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "BrewPiStructs.h"
#include "BrewPiTicks.h"

// ========================================
// CLASSE DE SENSOR DE TEMPERATURA
// ========================================

class TempSensor {
public:
    TempSensor(DallasTemperature* sensors, uint8_t sensorIndex);
    
    // Inicialização
    void init();
    
    // Atualização de leitura
    void update();
    
    // Verifica se está conectado
    bool isConnected() const {
        return connected;
    }
    
    // Leituras filtradas
    temperature readFastFiltered() const {
        return fastFiltered;
    }
    
    temperature readSlowFiltered() const {
        return slowFiltered;
    }
    
    temperature readSlope() const {
        return slope;
    }
    
    // Configuração de filtros
    void setFastFilterCoefficients(uint8_t coeff);
    void setSlowFilterCoefficients(uint8_t coeff);
    void setSlopeFilterCoefficients(uint8_t coeff);
    
    // Detecção de picos
    temperature detectPosPeak();
    temperature detectNegPeak();
    
private:
    // Hardware
    DallasTemperature* sensors;
    uint8_t sensorIndex;
    bool connected;
    
    // Leituras
    temperature currentTemp;
    temperature fastFiltered;
    temperature slowFiltered;
    temperature slope;
    
    // Filtros
    uint8_t fastFilterCoeff;
    uint8_t slowFilterCoeff;
    uint8_t slopeFilterCoeff;
    
    // Histórico para slope
    temperature prevSlowFiltered;
    ticks_seconds_t lastSlopeUpdate;
    
    // Detecção de picos
    bool posPeakDetected;
    bool negPeakDetected;
    temperature posPeak;
    temperature negPeak;
    temperature prevTemp;
    
    // Função auxiliar de filtro
    temperature applyFilter(temperature input, temperature prevOutput, uint8_t coeff);
};