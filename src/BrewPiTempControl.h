// BrewPiTempControl.h
#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include "BrewPiStructs.h"
#include "BrewPiTicks.h"
#include "TempSensor.h"
#include "estruturas.h"  // Para Rele
#include "controle_temperatura.h"  // Para DetailedControlStatus

// ========================================
// CLASSE DE CONTROLE DE TEMPERATURA
// ========================================

class BrewPiTempControl {
public:
    // Construtor
    BrewPiTempControl();
    
    // Inicialização
    void init();
    
    // Reset do controle
    void reset();
    
    // Ciclo principal de controle
    void update();
    
    // Configuração de sensores
    void setSensors(DallasTemperature* sensors, uint8_t beerIdx, uint8_t fridgeIdx);
    
    // Configuração de atuadores
    void setActuators(Rele* cool, Rele* heat);
    
    // Getters de temperatura
    temperature getBeerTemp() const;
    temperature getBeerSetting() const;
    temperature getFridgeTemp() const;
    temperature getFridgeSetting() const;
    
    // Setters de temperatura
    void setBeerTemp(temperature newTemp);
    void setFridgeTemp(temperature newTemp);
    
    // Modo de controle
    void setMode(char newMode, bool force = false);
    char getMode() const { return cs.mode; }
    
    // Estado
    uint8_t getState() const { return state; }
    bool stateIsCooling() const { return (state == COOLING || state == COOLING_MIN_TIME); }
    bool stateIsHeating() const { return (state == HEATING || state == HEATING_MIN_TIME); }
    bool modeIsBeer() const { return (cs.mode == MODE_BEER_CONSTANT || cs.mode == MODE_BEER_PROFILE); }
    
    // Tempo de espera
    uint16_t getWaitTime() const { return waitTime; }
    
    // Constantes e configurações públicas
    ControlConstants cc;
    ControlSettings cs;
    ControlVariables cv;
    
    // Status detalhado para frontend
    DetailedControlStatus getDetailedStatus();
    
    // Carrega constantes padrão
    void loadDefaultConstants();
    void loadDefaultSettings();
    
private:
    // Funções internas
    void updateTemperatures();
    void updatePID();
    void updateState();
    void updateOutputs();
    void detectPeaks();
    void updateEstimatedPeak(uint16_t timeLimit, temperature estimator, uint16_t sinceIdle);
    void initFilters();
    
    // Funções auxiliares
    void increaseEstimator(temperature* estimator, temperature error);
    void decreaseEstimator(temperature* estimator, temperature error);
    uint16_t timeSinceCooling() const;
    uint16_t timeSinceHeating() const;
    uint16_t timeSinceIdle() const;
    void resetWaitTime() { waitTime = 0; }
    void updateWaitTime(uint16_t newTimeLimit, uint16_t newTimeSince);
    
    // Sensores
    TempSensor* beerSensor;
    TempSensor* fridgeSensor;
    
    // Atuadores
    Rele* cooler;
    Rele* heater;
    
    // Estado da máquina
    uint8_t state;
    bool doPosPeakDetect;
    bool doNegPeakDetect;
    
    // Timestamps
    ticks_seconds_t lastIdleTime;
    ticks_seconds_t lastHeatTime;
    ticks_seconds_t lastCoolTime;
    uint16_t waitTime;
    
    // Temperatura armazenada
    temperature storedBeerSetting;
};

// Instância global
extern BrewPiTempControl brewPiControl;
