// fermentacao_stages.h - Processamento local de etapas
#pragma once

#include <Arduino.h>
#include "definitions.h"  // Importa todas as definições

// Forward declarations
struct FermentationStage;
struct FermentacaoState;
struct SystemState;

// Funções principais de processamento de etapas
// Cada função retorna true quando a etapa está concluída
bool processCurrentStage(const FermentationStage& stage, float elapsedDays, 
                        float elapsedHours, bool targetReached);

// Verifica e notifica quando temperatura alvo é atingida
void checkAndSendTargetReached();

// Envia resumo inicial das etapas ao MySQL
void sendStagesSummary();