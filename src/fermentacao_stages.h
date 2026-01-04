#pragma once
#include <Arduino.h>

// Forward declarations para estruturas usadas
struct FermentationStage;
struct FermentacaoState;
struct SystemState;

// Constantes
#ifndef TEMPERATURE_TOLERANCE
#define TEMPERATURE_TOLERANCE 0.5f
#endif

// Funções de controle de estágios
bool canSendFirebaseUpdate();
void checkAndSendTargetReached();
void sendStageTimers();
bool processCurrentStage(const FermentationStage& stage, float elapsedDays, float elapsedHours);

