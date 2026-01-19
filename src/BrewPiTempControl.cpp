// BrewPiTempControl.cpp - Implementação COMPLETA do controle BrewPi
#include "BrewPiTempControl.h"
#include "globais.h"
#include "debug_config.h"

// Definição da instância global
BrewPiTempControl brewPiControl;

// ========================================
// CONSTRUTOR E INICIALIZAÇÃO
// ========================================

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
    
    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] ✅ Sistema inicializado"));
    #endif
}

void BrewPiTempControl::reset() {
    doPosPeakDetect = false;
    doNegPeakDetect = false;
    
    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] 🔄 Controle resetado"));
    #endif
}

void BrewPiTempControl::setSensors(DallasTemperature* sensors, uint8_t beerIdx, uint8_t fridgeIdx) {
    if (beerSensor) delete beerSensor;
    if (fridgeSensor) delete fridgeSensor;
    
    beerSensor = new TempSensor(sensors, beerIdx);
    fridgeSensor = new TempSensor(sensors, fridgeIdx);
    
    beerSensor->init();
    fridgeSensor->init();
    
    initFilters();
    
    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] 📡 Sensores configurados"));
    #endif
}

void BrewPiTempControl::setActuators(Rele* cool, Rele* heat) {
    cooler = cool;
    heater = heat;
    
        #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] ⚡ Atuadores configurados"));
    #endif
}

void BrewPiTempControl::loadDefaultConstants() {
    cc = DEFAULT_CONTROL_CONSTANTS;

    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] 📋 Constantes padrão carregadas"));
    #endif
}

void BrewPiTempControl::loadDefaultSettings() {
    cs = DEFAULT_CONTROL_SETTINGS;
    storedBeerSetting = cs.beerSetting;

    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] ⚙️  Configurações padrão carregadas"));
    #endif
}

void BrewPiTempControl::initFilters() {
    if (!beerSensor || !fridgeSensor) return;
    
    fridgeSensor->setFastFilterCoefficients(cc.fridgeFastFilter);
    fridgeSensor->setSlowFilterCoefficients(cc.fridgeSlowFilter);
    fridgeSensor->setSlopeFilterCoefficients(cc.fridgeSlopeFilter);
    
    beerSensor->setFastFilterCoefficients(cc.beerFastFilter);
    beerSensor->setSlowFilterCoefficients(cc.beerSlowFilter);
    beerSensor->setSlopeFilterCoefficients(cc.beerSlopeFilter);
    
    #if DEBUG_BREWPI
    Serial.println(F("[BrewPi] 🔧 Filtros inicializados"));
    #endif
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
// CONTROLE PID (100% FIEL AO BREWPI)
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
        #if DEBUG_BREWPI
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
        #endif
    }
    else if (cs.mode == MODE_FRIDGE_CONSTANT) {
        cs.beerSetting = INVALID_TEMP;
    }
}

// ========================================
// MÁQUINA DE ESTADOS (100% BREWPI)
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
            
            // ✅ COMPARAÇÃO CORRETA: FRIDGE vs FRIDGE SETTING
            if (fridgeFast > (cs.fridgeSetting + cc.idleRangeHigh)) {
                updateWaitTime(cc.mutexDeadTime, sinceHeating);
                
                if (cs.mode == MODE_FRIDGE_CONSTANT) {
                    updateWaitTime(600, sinceCooling);
                } else {
                    // Proteção: se cerveja já está fria, não resfria mais
                    if (beerFast < (cs.beerSetting + 16)) {
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
                updateWaitTime(cc.mutexDeadTime, sinceCooling);
                updateWaitTime(cc.minHeatIdleTime, sinceHeating);
                
                if (cs.mode != MODE_FRIDGE_CONSTANT) {
                    // Proteção: se cerveja já está quente, não aquece mais
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

// ========================================
// FUNÇÕES AUXILIARES
// ========================================

void BrewPiTempControl::updateEstimatedPeak(uint16_t timeLimit, temperature estimator, uint16_t sinceIdle) {
    uint16_t activeTime = min(timeLimit, sinceIdle);
    temperature estimatedOvershoot = ((long_temperature)estimator * activeTime) / 3600;
    
    if (stateIsCooling()) {
        estimatedOvershoot = -estimatedOvershoot;
    }
    
    cv.estimatedPeak = fridgeSensor->readFastFiltered() + estimatedOvershoot;
}

void BrewPiTempControl::updateWaitTime(uint16_t newTimeLimit, uint16_t newTimeSince) {
    if (newTimeSince < newTimeLimit) {
        uint16_t newWaitTime = newTimeLimit - newTimeSince;
        if (newWaitTime > waitTime) {
            waitTime = newWaitTime;
        }
    }
}

uint16_t BrewPiTempControl::timeSinceCooling() const {
    return ticks.timeSince(lastCoolTime);
}

uint16_t BrewPiTempControl::timeSinceHeating() const {
    return ticks.timeSince(lastHeatTime);
}

uint16_t BrewPiTempControl::timeSinceIdle() const {
    return ticks.timeSince(lastIdleTime);
}

// ========================================
// DETECÇÃO DE PICOS
// ========================================

void BrewPiTempControl::increaseEstimator(temperature* estimator, temperature error) {
    // Aumenta estimador em 20%-50% baseado no erro
    temperature factor = 614 + constrainTemp(abs((int)error)>>5, 0, 154);  // 1.2 + 3.1% do erro
    *estimator = multiplyFactorTemperatureDiff(factor, *estimator);
    
    if (*estimator < 25) {
        *estimator = intToTempDiff(5)/100;  // Mínimo 0.05
    }
    
    #if DEBUG_BREWPI
    Serial.printf("[BrewPi] ↑ Estimador aumentado para %.3f\n", tempToFloat(*estimator));
    #endif
}

void BrewPiTempControl::decreaseEstimator(temperature* estimator, temperature error) {
    // Diminui estimador em 16.7%-33.3% baseado no erro
    temperature factor = 426 - constrainTemp(abs((int)error)>>5, 0, 85);  // 0.833 - 3.1% do erro
    *estimator = multiplyFactorTemperatureDiff(factor, *estimator);
    
    #if DEBUG_BREWPI
    Serial.printf("[BrewPi] ↓ Estimador diminuído para %.3f\n", tempToFloat(*estimator));
    #endif
}

void BrewPiTempControl::detectPeaks() {
    temperature peak, estimate, error;
    
    // Detecção de pico positivo (após aquecimento)
    if (doPosPeakDetect && !stateIsHeating()) {
        peak = fridgeSensor->detectPosPeak();
        estimate = cv.posPeakEstimate;
        error = peak - estimate;
        
        if (peak != INVALID_TEMP) {
            // Pico positivo detectado
            if (error > cc.heatingTargetUpper) {
                // Overshoot maior que esperado
                increaseEstimator(&(cs.heatEstimator), error);
            }
            else if (error < cc.heatingTargetLower) {
                // Overshoot menor que esperado
                decreaseEstimator(&(cs.heatEstimator), error);
            }
            
            cv.posPeak = peak;
            doPosPeakDetect = false;
          
            #if DEBUG_BREWPI
            Serial.printf("[BrewPi] 🔺 Pico positivo: %.2f°C (esperado: %.2f°C)\n",
                         tempToFloat(peak), tempToFloat(estimate));
            #endif
        }
        else if (timeSinceHeating() > HEAT_PEAK_DETECT_TIME) {
            // Timeout - sem pico detectado
            doPosPeakDetect = false;
        }
    }
    
    // Detecção de pico negativo (após resfriamento)
    if (doNegPeakDetect && !stateIsCooling()) {
        peak = fridgeSensor->detectNegPeak();
        estimate = cv.negPeakEstimate;
        error = peak - estimate;
        
        if (peak != INVALID_TEMP) {
            // Pico negativo detectado
            if (error < cc.coolingTargetLower) {
                // Overshoot maior que esperado
                increaseEstimator(&(cs.coolEstimator), error);
            }
            else if (error > cc.coolingTargetUpper) {
                // Overshoot menor que esperado
                decreaseEstimator(&(cs.coolEstimator), error);
            }
            
            cv.negPeak = peak;
            doNegPeakDetect = false;
            
            #if DEBUG_BREWPI
            Serial.printf("[BrewPi] 🔻 Pico negativo: %.2f°C (esperado: %.2f°C)\n",
                         tempToFloat(peak), tempToFloat(estimate));
            #endif
        }
        else if (timeSinceCooling() > COOL_PEAK_DETECT_TIME) {
            // Timeout - sem pico detectado
            doNegPeakDetect = false;
        }
    }
}

// ========================================
// ATUALIZAÇÃO DE SAÍDAS
// ========================================

void BrewPiTempControl::updateOutputs() {
    if (cs.mode == MODE_TEST) return;
    
    bool cooling = stateIsCooling();
    bool heating = stateIsHeating();
    
    if (cooler) {
        cooler->estado = cooling;
        cooler->atualizar();
    }
    
    if (heater) {
        heater->estado = heating;
        heater->atualizar();
    }
}

// ========================================
// FUNÇÃO PRINCIPAL DE ATUALIZAÇÃO
// ========================================

void BrewPiTempControl::update() {
    updateTemperatures();
    updatePID();
    updateState();
    detectPeaks();
    updateOutputs();
}

// ========================================
// GETTERS
// ========================================

temperature BrewPiTempControl::getBeerTemp() const {
    if (!beerSensor || !beerSensor->isConnected()) return INVALID_TEMP;
    return beerSensor->readFastFiltered();
}

temperature BrewPiTempControl::getBeerSetting() const {
    return cs.beerSetting;
}

temperature BrewPiTempControl::getFridgeTemp() const {
    if (!fridgeSensor || !fridgeSensor->isConnected()) return INVALID_TEMP;
    return fridgeSensor->readFastFiltered();
}

temperature BrewPiTempControl::getFridgeSetting() const {
    return cs.fridgeSetting;
}

// ========================================
// SETTERS
// ========================================

void BrewPiTempControl::setBeerTemp(temperature newTemp) {
    temperature oldBeerSetting = cs.beerSetting;
    cs.beerSetting = newTemp;
    
    // Reset se mudança significativa (>0.5°C)
    if (abs(oldBeerSetting - newTemp) > intToTempDiff(1)/2) {
        reset();
    }
    
    updatePID();
    updateState();
    storedBeerSetting = newTemp;
    
    #if DEBUG_BREWPI
    Serial.printf("[BrewPi] 🎯 Beer setting: %.2f°C\n", tempToFloat(newTemp));
    #endif
}

void BrewPiTempControl::setFridgeTemp(temperature newTemp) {
    cs.fridgeSetting = newTemp;
    reset();
    updatePID();
    updateState();
    
    #if DEBUG_BREWPI
    Serial.printf("[BrewPi] ❄️  Fridge setting: %.2f°C\n", tempToFloat(newTemp));
    #endif
}

void BrewPiTempControl::setMode(char newMode, bool force) {
    if (newMode != cs.mode || 
        state == WAITING_TO_HEAT || 
        state == WAITING_TO_COOL || 
        state == WAITING_FOR_PEAK_DETECT) {
        state = IDLE;
        force = true;
    }
    
    if (force) {
        cs.mode = newMode;
        
        if (newMode == MODE_OFF) {
            cs.beerSetting = INVALID_TEMP;
            cs.fridgeSetting = INVALID_TEMP;
        }
        
        #if DEBUG_BREWPI
        Serial.printf("[BrewPi] 📋 Modo alterado para: %c\n", newMode);
        #endif
    }
}

// ========================================
// STATUS DETALHADO
// ========================================

DetailedControlStatus BrewPiTempControl::getDetailedStatus() {
    DetailedControlStatus status;
    
    // Estado dos atuadores
    status.coolerActive = stateIsCooling();
    status.heaterActive = stateIsHeating();
    
    // Pico estimado
    status.estimatedPeak = tempToFloat(cv.estimatedPeak);
    status.peakDetection = (doPosPeakDetect || doNegPeakDetect);
    
    // Estado da máquina
    status.isWaiting = false;
    status.waitTimeRemaining = 0;
    status.waitReason = "";
    
    switch (state) {
        case STATE_OFF:
            status.stateName = "DESLIGADO";
            break;
        case IDLE:
            status.stateName = "IDLE";
            break;
        case COOLING:
            status.stateName = "RESFRIANDO";
            break;
        case HEATING:
            status.stateName = "AQUECENDO";
            break;
        case WAITING_TO_COOL:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitTimeRemaining = waitTime;
            status.waitReason = "Proteção: intervalo mínimo resfriamento";
            break;
        case WAITING_TO_HEAT:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitTimeRemaining = waitTime;
            status.waitReason = "Proteção: intervalo mínimo aquecimento";
            break;
        case WAITING_FOR_PEAK_DETECT:
            status.stateName = "AGUARDANDO";
            status.isWaiting = true;
            status.waitReason = "Aguardando estabilização (detecção de pico)";
            status.peakDetection = true;
            break;
        case COOLING_MIN_TIME:
            status.stateName = "RESFRIANDO";
            status.isWaiting = true;
            status.waitReason = "Tempo mínimo de resfriamento";
            break;
        case HEATING_MIN_TIME:
            status.stateName = "AQUECENDO";
            status.isWaiting = true;
            status.waitReason = "Tempo mínimo de aquecimento";
            break;
        default:
            status.stateName = "DESCONHECIDO";
            break;
    }
    
    return status;
}
