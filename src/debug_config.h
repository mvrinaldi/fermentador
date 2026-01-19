// debug_config.h
#pragma once

// ============================================
// CONFIGURAÇÃO DE DEBUG - ESCOLHA UMA OPÇÃO
// ============================================

// OPÇÃO 1: Controle global simples
#define DEBUG_MODE 1  // 0=Produção, 1=Desenvolvimento

// OPÇÃO 2: Controle por módulo (recomendado)
#define DEBUG_HTTP          0  // Debug HTTP Client
#define DEBUG_FERMENTATION  0  // Debug Fermentação
#define DEBUG_SENSORES      0  // Debug Sensores
#define DEBUG_BREWPI        0  // Debug BrewPi
#define DEBUG_EEPROM        0  // Debug EEPROM
#define DEBUG_MAIN          0  // Debug Main
#define DEBUG_HEARTBEAT     0  // Debug Heartbeat
#define DEBUG_ENVIODADOS    1  // Debug Envio de Dados 
