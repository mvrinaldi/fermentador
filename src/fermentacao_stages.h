// fermentacao_stages.h - Processamento local de etapas
#pragma once

#include <Arduino.h>

// Forward declarations
struct FermentationStage;
struct FermentacaoState;
struct SystemState;

// Constantes (apenas se não definidas)
#ifndef TEMPERATURE_TOLERANCE
#define TEMPERATURE_TOLERANCE 0.5f
#endif

// Funções principais de processamento de etapas
// Cada função retorna true quando a etapa está concluída
bool processCurrentStage(const FermentationStage& stage, float elapsedDays, 
                        float elapsedHours, bool targetReached);

// Verifica e notifica quando temperatura alvo é atingida
void checkAndSendTargetReached();

// Envia resumo inicial das etapas ao MySQL
void sendStagesSummary();