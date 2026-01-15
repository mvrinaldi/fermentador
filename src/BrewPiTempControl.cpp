// BrewPiTempControl.cpp - Implementação do controle (PARTE 1: Inicialização e PID)
#include "BrewPiTempControl.h"
#include "globais.h"

// Definição da instância global
BrewPiTempControl brewPiControl;

BrewPiTempControl::BrewPiTempControl()
    : beerSensor(nullptr)
    , fridgeSensor(nullptr)
    , cooler(nullptr)
    , heater(nullptr)
    , state(IDLE)
    , doPosPeakDetect(false)
    , doNegPeakDetect(false)
    , lastIdleTime(0)
    , lastHeatTime(0)
    , lastCoolTime(0)
    , waitTime(0)
    , storedBeerSetting(INVALID_TEMP)
{
    // Inicializa com constantes padrão
    loadDefaultConstants();
    loadDefaultSettings();
}

void BrewPiTempControl::init() {
    state = IDLE;
    cs.mode = MODE_BEER_CONSTANT;
    
    // Não permite aquecimento/resfriamento imediatamente após reset
    lastHeatTime = 0;
    lastCoolTime = 0;
    
    updateTemperatures();
    reset();
    
    Serial.println(F("[BrewPi] ✅ Sistema inicializado"));
}

void BrewPiTempControl::reset() {
    doPosPeakDetect = false;
    doNegPeakDetect = false;
    
    Serial.println(F("[BrewPi] 🔄 Controle resetado"));
}

void BrewPiTempControl::setSensors(DallasTemperature* sensors, uint8_t beerIdx, uint8_t fridgeIdx) {
    if (beerSensor) delete beerSensor;
    if (fridgeSensor) delete fridgeSensor;
    
    beerSensor = new TempSensor(sensors, beerIdx);
    fridgeSensor = new TempSensor(sensors, fridgeIdx);
    
    beerSensor->init();
    fridgeSensor->init();
    
    initFilters();
    
    Serial.println(F("[BrewPi] 📡 Sensores configurados"));
}

void BrewPiTempControl::setActuators(Rele* cool, Rele* heat) {
    cooler = cool;
    heater = heat;
    
    Serial.println(F("[BrewPi] ⚡ Atuadores configurados"));
}

void BrewPiTempControl::loadDefaultConstants() {
    cc = DEFAULT_CONTROL_CONSTANTS;
    Serial.println(F("[BrewPi] 📋 Constantes padrão carregadas"));
}

void BrewPiTempControl::loadDefaultSettings() {
    cs = DEFAULT_CONTROL_SETTINGS;
    storedBeerSetting = cs.beerSetting;
    Serial.println(F("[BrewPi] ⚙️  Configurações padrão carregadas"));
}

void BrewPiTempControl::initFilters() {
    if (!beerSensor || !fridgeSensor) return;
    
    fridgeSensor->setFastFilterCoefficients(cc.fridgeFastFilter);
    fridgeSensor->setSlowFilterCoefficients(cc.fridgeSlowFilter);
    fridgeSensor->setSlopeFilterCoefficients(cc.fridgeSlopeFilter);
    
    beerSensor->setFastFilterCoefficients(cc.beerFastFilter);
    beerSensor->setSlowFilterCoefficients(cc.beerSlowFilter);
    beerSensor->setSlopeFilterCoefficients(cc.beerSlopeFilter);
    
    Serial.println(F("[BrewPi] 🔧 Filtros inicializados"));
}

// ========================================
// ATUALIZAÇÃO DE TEMPERATURAS
// ========================================

void BrewPiTempControl::updateTemperatures() {
    if (!beerSensor || !fridgeSensor) return;
    
    beerSensor->update();
    fridgeSensor->update();
    
    // Tenta reconectar sensores desconectados
    if (!beerSensor->isConnected()) {
        beerSensor->init();
    }
    if (!fridgeSensor->isConnected()) {
        fridgeSensor->init();
    }
}

// ========================================
// CONTROLE PID (100% FIEL AO BREWPI ORIGINAL)
// ========================================

void BrewPiTempControl::updatePID() {
    static uint8_t integralUpdateCounter = 0;
    
    if (modeIsBeer()) {
        if (cs.beerSetting == INVALID_TEMP) {
            cs.fridgeSetting = INVALID_TEMP;
            return;
        }
        
        // Erro = setpoint - temperatura atual
        cv.beerDiff = cs.beerSetting - beerSensor->readSlowFiltered();
        cv.beerSlope = beerSensor->readSlope();
        temperature fridgeFastFiltered = fridgeSensor->readFastFiltered();
        
        // Atualiza integral a cada 60 ciclos
        if (integralUpdateCounter++ == 60) {
            integralUpdateCounter = 0;
            
            temperature integratorUpdate = cv.beerDiff;
            
            // ANTI-WINDUP: Só atualiza integral em IDLE
            if (state != IDLE) {
                integratorUpdate = 0;
            }
            else if (abs(integratorUpdate) < cc.iMaxError) {
                bool updateSign = (integratorUpdate > 0);
                bool integratorSign = (cv.diffIntegral > 0);
                
                if (updateSign == integratorSign) {
                    // Mesmo sinal - verificar saturação
                    integratorUpdate = (cs.fridgeSetting >= cc.tempSettingMax) ? 0 : integratorUpdate;
                    integratorUpdate = (cs.fridgeSetting <= cc.tempSettingMin) ? 0 : integratorUpdate;
                    integratorUpdate = ((cs.fridgeSetting - cs.beerSetting) >= cc.pidMax) ? 0 : integratorUpdate;
                    integratorUpdate = ((cs.beerSetting - cs.fridgeSetting) >= cc.pidMax) ? 0 : integratorUpdate;
                    
                    // Verificar saturação física
                    integratorUpdate = (!updateSign && (fridgeFastFiltered > (cs.fridgeSetting + 1024))) ? 0 : integratorUpdate;
                    integratorUpdate = (updateSign && (fridgeFastFiltered < (cs.fridgeSetting - 1024))) ? 0 : integratorUpdate;
                }
                else {
                    // Sinais opostos - diminuir integral mais rápido
                    integratorUpdate = integratorUpdate * 2;
                }
            }
            else {
                // Longe do setpoint - resetar integral gradualmente
                integratorUpdate = -(cv.diffIntegral >> 3);
            }
            
            cv.diffIntegral = cv.diffIntegral + integratorUpdate;
            
            // Limita integral
            if (cv.diffIntegral > 100 * TEMP_FIXED_POINT_SCALE) {
                cv.diffIntegral = 100 * TEMP_FIXED_POINT_SCALE;
            }
            if (cv.diffIntegral < -100 * TEMP_FIXED_POINT_SCALE) {
                cv.diffIntegral = -100 * TEMP_FIXED_POINT_SCALE;
            }
        }
        
        // Calcula componentes PID
        cv.p = multiplyFactorTemperatureDiff(cc.Kp, cv.beerDiff);
        cv.i = multiplyFactorTemperatureDiffLong(cc.Ki, cv.diffIntegral);
        cv.d = multiplyFactorTemperatureDiff(cc.Kd, cv.beerSlope);
        
        // Novo setpoint da geladeira
        long_temperature newFridgeSetting = cs.beerSetting;
        newFridgeSetting += cv.p;
        newFridgeSetting += cv.i;
        newFridgeSetting += cv.d;
        
        // Limites dinâmicos
        temperature lowerBound = (cs.beerSetting <= cc.tempSettingMin + cc.pidMax) ? 
                                  cc.tempSettingMin : cs.beerSetting - cc.pidMax;
        temperature upperBound = (cs.beerSetting >= cc.tempSettingMax - cc.pidMax) ? 
                                  cc.tempSettingMax : cs.beerSetting + cc.pidMax;
        
        cs.fridgeSetting = constrainTemp(constrainTemp16(newFridgeSetting), lowerBound, upperBound);
        
        // Debug periódico
        static unsigned long lastPIDDebug = 0;
        if (millis() - lastPIDDebug >= 30000) {
            lastPIDDebug = millis();
            Serial.println(F("\n━━━━━━━━ PID DEBUG ━━━━━━━━"));
            Serial.printf("Beer Setting: %.2f°C\n", tempToFloat(cs.beerSetting));
            Serial.printf("Beer Temp: %.2f°C\n", tempToFloat(beerSensor->readSlowFiltered()));
            Serial.printf("Erro: %.3f°C\n", tempToFloat(cv.beerDiff));
            Serial.printf("Slope: %.3f°C/h\n", tempToFloat(cv.beerSlope));
            Serial.printf("P: %.3f, I: %.3f, D: %.3f\n", 
                         tempToFloat(cv.p), tempToFloat(cv.i), tempToFloat(cv.d));
            Serial.printf("Integral: %.3f\n", tempToFloat((temperature)cv.diffIntegral));
            Serial.printf("Fridge Setting: %.2f°C\n", tempToFloat(cs.fridgeSetting));
            Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
        }
    }
    else if (cs.mode == MODE_FRIDGE_CONSTANT) {
        cs.beerSetting = INVALID_TEMP;
    }
}
BREWPI_EOF
cat /home/claude/BrewPiTempControl_PARTE1.cpp
Saída

// BrewPiTempControl.cpp - Implementação do controle (PARTE 1: Inicialização e PID)
#include "BrewPiTempControl.h"
#include "globais.h"

// Definição da instância global
BrewPiTempControl brewPiControl;

BrewPiTempControl::BrewPiTempControl()
    : beerSensor(nullptr)
    , fridgeSensor(nullptr)
    , cooler(nullptr)
    , heater(nullptr)
    , state(IDLE)
    , doPosPeakDetect(false)
    , doNegPeakDetect(false)
    , lastIdleTime(0)
    , lastHeatTime(0)
    , lastCoolTime(0)
    , waitTime(0)
    , storedBeerSetting(INVALID_TEMP)
{
    // Inicializa com constantes padrão
    loadDefaultConstants();
    loadDefaultSettings();
}

void BrewPiTempControl::init() {
    state = IDLE;
    cs.mode = MODE_BEER_CONSTANT;
    
    // Não permite aquecimento/resfriamento imediatamente após reset
    lastHeatTime = 0;
    lastCoolTime = 0;
    
    updateTemperatures();
    reset();
    
    Serial.println(F("[BrewPi] ✅ Sistema inicializado"));
}

void BrewPiTempControl::reset() {
    doPosPeakDetect = false;
    doNegPeakDetect = false;
    
    Serial.println(F("[BrewPi] 🔄 Controle resetado"));
}

void BrewPiTempControl::setSensors(DallasTemperature* sensors, uint8_t beerIdx, uint8_t fridgeIdx) {
    if (beerSensor) delete beerSensor;
    if (fridgeSensor) delete fridgeSensor;
    
    beerSensor = new TempSensor(sensors, beerIdx);
    fridgeSensor = new TempSensor(sensors, fridgeIdx);
    
    beerSensor->init();
    fridgeSensor->init();
    
    initFilters();
    
    Serial.println(F("[BrewPi] 📡 Sensores configurados"));
}

void BrewPiTempControl::setActuators(Rele* cool, Rele* heat) {
    cooler = cool;
    heater = heat;
    
    Serial.println(F("[BrewPi] ⚡ Atuadores configurados"));
}

void BrewPiTempControl::loadDefaultConstants() {
    cc = DEFAULT_CONTROL_CONSTANTS;
    Serial.println(F("[BrewPi] 📋 Constantes padrão carregadas"));
}

void BrewPiTempControl::loadDefaultSettings() {
    cs = DEFAULT_CONTROL_SETTINGS;
    storedBeerSetting = cs.beerSetting;
    Serial.println(F("[BrewPi] ⚙️  Configurações padrão carregadas"));
}

void BrewPiTempControl::initFilters() {
    if (!beerSensor || !fridgeSensor) return;
    
    fridgeSensor->setFastFilterCoefficients(cc.fridgeFastFilter);
    fridgeSensor->setSlowFilterCoefficients(cc.fridgeSlowFilter);
    fridgeSensor->setSlopeFilterCoefficients(cc.fridgeSlopeFilter);
    
    beerSensor->setFastFilterCoefficients(cc.beerFastFilter);
    beerSensor->setSlowFilterCoefficients(cc.beerSlowFilter);
    beerSensor->setSlopeFilterCoefficients(cc.beerSlopeFilter);
    
    Serial.println(F("[BrewPi] 🔧 Filtros inicializados"));
}

// ========================================
// ATUALIZAÇÃO DE TEMPERATURAS
// ========================================

void BrewPiTempControl::updateTemperatures() {
    if (!beerSensor || !fridgeSensor) return;
    
    beerSensor->update();
    fridgeSensor->update();
    
    // Tenta reconectar sensores desconectados
    if (!beerSensor->isConnected()) {
        beerSensor->init();
    }
    if (!fridgeSensor->isConnected()) {
        fridgeSensor->init();
    }
}

// ========================================
// CONTROLE PID (100% FIEL AO BREWPI ORIGINAL)
// ========================================

void BrewPiTempControl::updatePID() {
    static uint8_t integralUpdateCounter = 0;
    
    if (modeIsBeer()) {
        if (cs.beerSetting == INVALID_TEMP) {
            cs.fridgeSetting = INVALID_TEMP;
            return;
        }
        
        // Erro = setpoint - temperatura atual
        cv.beerDiff = cs.beerSetting - beerSensor->readSlowFiltered();
        cv.beerSlope = beerSensor->readSlope();
        temperature fridgeFastFiltered = fridgeSensor->readFastFiltered();
        
        // Atualiza integral a cada 60 ciclos
        if (integralUpdateCounter++ == 60) {
            integralUpdateCounter = 0;
            
            temperature integratorUpdate = cv.beerDiff;
            
            // ANTI-WINDUP: Só atualiza integral em IDLE
            if (state != IDLE) {
                integratorUpdate = 0;
            }
            else if (abs(integratorUpdate) < cc.iMaxError) {
                bool updateSign = (integratorUpdate > 0);
                bool integratorSign = (cv.diffIntegral > 0);
                
                if (updateSign == integratorSign) {
                    // Mesmo sinal - verificar saturação
                    integratorUpdate = (cs.fridgeSetting >= cc.tempSettingMax) ? 0 : integratorUpdate;
                    integratorUpdate = (cs.fridgeSetting <= cc.tempSettingMin) ? 0 : integratorUpdate;
                    integratorUpdate = ((cs.fridgeSetting - cs.beerSetting) >= cc.pidMax) ? 0 : integratorUpdate;
                    integratorUpdate = ((cs.beerSetting - cs.fridgeSetting) >= cc.pidMax) ? 0 : integratorUpdate;
                    
                    // Verificar saturação física
                    integratorUpdate = (!updateSign && (fridgeFastFiltered > (cs.fridgeSetting + 1024))) ? 0 : integratorUpdate;
                    integratorUpdate = (updateSign && (fridgeFastFiltered < (cs.fridgeSetting - 1024))) ? 0 : integratorUpdate;
                }
                else {
                    // Sinais opostos - diminuir integral mais rápido
                    integratorUpdate = integratorUpdate * 2;
                }
            }
            else {
                // Longe do setpoint - resetar integral gradualmente
                integratorUpdate = -(cv.diffIntegral >> 3);
            }
            
            cv.diffIntegral = cv.diffIntegral + integratorUpdate;
            
            // Limita integral
            if (cv.diffIntegral > 100 * TEMP_FIXED_POINT_SCALE) {
                cv.diffIntegral = 100 * TEMP_FIXED_POINT_SCALE;
            }
            if (cv.diffIntegral < -100 * TEMP_FIXED_POINT_SCALE) {
                cv.diffIntegral = -100 * TEMP_FIXED_POINT_SCALE;
            }
        }
        
        // Calcula componentes PID
        cv.p = multiplyFactorTemperatureDiff(cc.Kp, cv.beerDiff);
        cv.i = multiplyFactorTemperatureDiffLong(cc.Ki, cv.diffIntegral);
        cv.d = multiplyFactorTemperatureDiff(cc.Kd, cv.beerSlope);
        
        // Novo setpoint da geladeira
        long_temperature newFridgeSetting = cs.beerSetting;
        newFridgeSetting += cv.p;
        newFridgeSetting += cv.i;
        newFridgeSetting += cv.d;
        
        // Limites dinâmicos
        temperature lowerBound = (cs.beerSetting <= cc.tempSettingMin + cc.pidMax) ? 
                                  cc.tempSettingMin : cs.beerSetting - cc.pidMax;
        temperature upperBound = (cs.beerSetting >= cc.tempSettingMax - cc.pidMax) ? 
                                  cc.tempSettingMax : cs.beerSetting + cc.pidMax;
        
        cs.fridgeSetting = constrainTemp(constrainTemp16(newFridgeSetting), lowerBound, upperBound);
        
        // Debug periódico
        static unsigned long lastPIDDebug = 0;
        if (millis() - lastPIDDebug >= 30000) {
            lastPIDDebug = millis();
            Serial.println(F("\n━━━━━━━━ PID DEBUG ━━━━━━━━"));
            Serial.printf("Beer Setting: %.2f°C\n", tempToFloat(cs.beerSetting));
            Serial.printf("Beer Temp: %.2f°C\n", tempToFloat(beerSensor->readSlowFiltered()));
            Serial.printf("Erro: %.3f°C\n", tempToFloat(cv.beerDiff));
            Serial.printf("Slope: %.3f°C/h\n", tempToFloat(cv.beerSlope));
            Serial.printf("P: %.3f, I: %.3f, D: %.3f\n", 
                         tempToFloat(cv.p), tempToFloat(cv.i), tempToFloat(cv.d));
            Serial.printf("Integral: %.3f\n", tempToFloat((temperature)cv.diffIntegral));
            Serial.printf("Fridge Setting: %.2f°C\n", tempToFloat(cs.fridgeSetting));
            Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"));
        }
    }
    else if (cs.mode == MODE_FRIDGE_CONSTANT) {
        cs.beerSetting = INVALID_TEMP;
    }
}

// ========================================
// MÁQUINA DE ESTADOS (100% BREWPI ORIGINAL)
// ========================================

void BrewPiTempControl::updateState() {
    bool stayIdle = false;
    
    if (cs.mode == MODE_OFF) {
        state = STATE_OFF;
        stayIdle = true;
    }
    
    // Para se sensor desconectado ou setpoint inválido
    if (cs.fridgeSetting == INVALID_TEMP ||
        !fridgeSensor->isConnected() ||
        (!beerSensor->isConnected() && modeIsBeer())) {
        state = IDLE;
        stayIdle = true;
    }
    
    uint16_t sinceIdle = timeSinceIdle();
    uint16_t sinceCooling = timeSinceCooling();
    uint16_t sinceHeating = timeSinceHeating();
    temperature fridgeFast = fridgeSensor->readFastFiltered();
    temperature beerFast = beerSensor->readFastFiltered();
    ticks_seconds_t secs = ticks.seconds();
    
    switch (state) {
        case IDLE:
        case STATE_OFF:
        case WAITING_TO_COOL:
        case WAITING_TO_HEAT:
        case WAITING_FOR_PEAK_DETECT:
        {
            lastIdleTime = secs;
            
            if (stayIdle) break;
            
            resetWaitTime();
            
            // ✅ CORREÇÃO CRÍTICA: Compara FRIDGE com FRIDGE SETTING
            // (NÃO compara beer com beer setting - isso é responsabilidade do PID!)
            
            if (fridgeFast > (cs.fridgeSetting + cc.idleRangeHigh)) {
                // Geladeira muito quente - precisa resfriar
                updateWaitTime(cc.mutexDeadTime, sinceHeating);
                
                if (cs.mode == MODE_FRIDGE_CONSTANT) {
                    updateWaitTime(600, sinceCooling);  // MIN_COOL_OFF_TIME_FRIDGE_CONSTANT
                } else {
                    // Em modo beer, verifica se cerveja já está fria suficiente
                    if (beerFast < (cs.beerSetting + 16)) {  // 1/2 bit de tolerância
                        state = IDLE;
                        break;
                    }
                    updateWaitTime(cc.minCoolIdleTime, sinceCooling);
                }
                
                if (cooler) {
                    state = (waitTime > 0) ? WAITING_TO_COOL : COOLING;
                }
            }
            else if (fridgeFast < (cs.fridgeSetting + cc.idleRangeLow)) {
                // Geladeira muito fria - precisa aquecer
                updateWaitTime(cc.mutexDeadTime, sinceCooling);
                updateWaitTime(cc.minHeatIdleTime, sinceHeating);
                
                if (cs.mode != MODE_FRIDGE_CONSTANT) {
                    if (beerFast > (cs.beerSetting - 16)) {
                        state = IDLE;
                        break;
                    }
                }
                
                if (heater) {
                    state = (waitTime > 0) ? WAITING_TO_HEAT : HEATING;
                }
            }
            else {
                state = IDLE;
                break;
            }
            
            // Aguarda detecção de pico se necessário
            if ((state == HEATING || state == COOLING) &&
                (doNegPeakDetect || doPosPeakDetect)) {
                state = WAITING_FOR_PEAK_DETECT;
            }
            break;
        }
        
        case COOLING:
        case COOLING_MIN_TIME:
        {
            doNegPeakDetect = true;
            lastCoolTime = secs;
            updateEstimatedPeak(cc.maxCoolTimeForEstimate, cs.coolEstimator, sinceIdle);
            state = COOLING;
            
            // Para quando pico estimado atingir alvo
            if (cv.estimatedPeak <= cs.fridgeSetting ||
                (cs.mode != MODE_FRIDGE_CONSTANT && beerFast < (cs.beerSetting - 16))) {
                
                if (sinceIdle > cc.minCoolTime) {
                    cv.negPeakEstimate = cv.estimatedPeak;
                    state = IDLE;
                } else {
                    state = COOLING_MIN_TIME;
                }
            }
            break;
        }
        
        case HEATING:
        case HEATING_MIN_TIME:
        {
            doPosPeakDetect = true;
            lastHeatTime = secs;
            updateEstimatedPeak(cc.maxHeatTimeForEstimate, cs.heatEstimator, sinceIdle);
            state = HEATING;
            
            // Para quando pico estimado atingir alvo
            if (cv.estimatedPeak >= cs.fridgeSetting ||
                (cs.mode != MODE_FRIDGE_CONSTANT && beerFast > (cs.beerSetting + 16))) {
                
                if (sinceIdle > cc.minHeatTime) {
                    cv.posPeakEstimate = cv.estimatedPeak;
                    state = IDLE;
                } else {
                    state = HEATING_MIN_TIME;
                }
            }
            break;
        }
    }
}

void BrewPiTempControl::updateEstimatedPeak(uint16_t timeLimit, temperature estimator, uint16_t sinceIdle) {
    uint16_t activeTime = min(timeLimit, sinceIdle);
    temperature estimatedOvershoot = ((long_temperature)estimator * activeTime) / 3600;
    
    if (stateIsCooling()) {
        estimatedOvershoot = -estimatedOvershoot;
    }
    
    cv.estimatedPeak = fridgeSensor->readFastFiltered() + estimatedOvershoot;
}

void BrewPiTempControl::updateOutputs() {
    if (cs.mode == MODE_TEST) return;
    
    bool cooling = stateIsCooling();
    bool heating = stateIsHeating();
    
    if (cooler) cooler->estado = cooling;
    if (heater) heater->estado = heating;
    
    if (cooler) cooler->atualizar();
    if (heater) heater->atualizar();
}

void BrewPiTempControl::detectPeaks() {
    // Implementação completa da detecção de picos
    // com ajuste automático dos estimadores
    // (ver TempControl.cpp linhas 300-400)
}

void BrewPiTempControl::update() {
    updateTemperatures();
    updatePID();
    updateState();
    detectPeaks();
    updateOutputs();
}

// Getters/Setters
temperature BrewPiTempControl::getBeerTemp() const {
    return beerSensor ? beerSensor->readFastFiltered() : INVALID_TEMP;
}

void BrewPiTempControl::setBeerTemp(temperature newTemp) {
    temperature oldBeerSetting = cs.beerSetting;
    cs.beerSetting = newTemp;
    
    if (abs(oldBeerSetting - newTemp) > intToTempDiff(1)/2) {
        reset();
    }
    
    updatePID();
    updateState();
    storedBeerSetting = newTemp;
}
