// controle_temperatura.h

#pragma once

#include <Arduino.h>

// ========================================
// ESTRUTURA DE STATUS DETALHADO
// ========================================

struct DetailedControlStatus {
    // Estado dos atuadores
    bool coolerActive;
    bool heaterActive;
    
    // Estado da máquina
    const char* stateName;      // "IDLE", "COOLING", "WAITING_TO_COOL", etc
    
    // Tempos de espera
    bool isWaiting;             // true se está em estado de espera
    uint16_t waitTimeRemaining; // Segundos restantes de espera
    const char* waitReason;     // "proteção compressor", "proteção heater", etc
    
    // Informações adicionais
    bool peakDetection;         // true se aguardando detecção de pico
    float estimatedPeak;        // Pico estimado (útil para debug)
};

// ========================================
// FUNÇÕES PÚBLICAS
// ========================================

/**
 * @brief Função principal de controle de temperatura
 * 
 * Executa o algoritmo PID adaptativo do BrewPiLess para controlar
 * temperatura através de cooler e heater.
 * 
 * Deve ser chamada periodicamente no loop() principal.
 */
void controle_temperatura();

/**
 * @brief Reseta o estado do PID
 * 
 * Limpa:
 * - Integral acumulado
 * - Filtros de temperatura
 * - Estimadores de pico
 * - Estados da máquina
 * - Desliga cooler e heater
 * 
 * Deve ser chamado:
 * - No início de nova fermentação
 * - Na mudança de etapa
 * - Na desativação de fermentação
 * - Na inicialização do sistema
 */
void resetPIDState();

/**
 * @brief Obtém status detalhado do controle
 * 
 * Retorna informações completas sobre:
 * - Estado atual dos atuadores (cooler/heater)
 * - Estado da máquina de estados
 * - Tempos de espera (proteções)
 * - Motivos das esperas
 * 
 * Útil para enviar ao frontend e mostrar ao usuário
 * exatamente o que o sistema está fazendo.
 * 
 * @return DetailedControlStatus struct com todas as informações
 * 
 * @example
 * DetailedControlStatus status = getDetailedStatus();
 * if (status.isWaiting) {
 *     Serial.printf("Aguardando: %s (%us restantes)\n",
 *                   status.waitReason,
 *                   status.waitTimeRemaining);
 * }
 */
DetailedControlStatus getDetailedStatus();