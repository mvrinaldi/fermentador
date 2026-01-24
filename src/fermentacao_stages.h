// fermentacao_stages.h - Processamento local de etapas
// ✅ CONSOLIDADO: Apenas lógica de processamento, sem envios
// Envios são feitos por enviarEstadoCompletoMySQL() em mysql_sender.cpp
#pragma once

#include <Arduino.h>
#include "definitions.h"

// Forward declarations
struct FermentationStage;
struct FermentacaoState;
struct SystemState;

// =====================================================
// FUNÇÕES DE PROCESSAMENTO DE ETAPAS
// Cada função retorna true quando a etapa está concluída
// NÃO fazem envios - apenas lógica de processamento
// =====================================================

// Processa etapa do tipo TEMPERATURE
bool handleTemperatureStage(const FermentationStage& stage, float elapsedDays, bool targetReached);

// Processa etapa do tipo RAMP
bool handleRampStage(const FermentationStage& stage, float elapsedHours);

// Processa etapa do tipo GRAVITY
bool handleGravityStage(const FermentationStage& stage);

// Processa etapa do tipo GRAVITY_TIME
bool handleGravityTimeStage(const FermentationStage& stage, float elapsedDays);

// =====================================================
// FUNÇÕES PRINCIPAIS
// =====================================================

// Processador principal - despacha para o handler correto
bool processCurrentStage(const FermentationStage& stage, float elapsedDays, 
                        float elapsedHours, bool targetReached);

// Verifica e notifica quando temperatura alvo é atingida
void checkAndSendTargetReached();

// Envia resumo das etapas ao MySQL (wrapper para mysql_sender)
void sendStagesSummary();