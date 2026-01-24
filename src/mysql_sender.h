// mysql_sender.h - Módulo de envio de dados para MySQL
// ✅ CONSOLIDADO: Removidas funções duplicadas
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Forward declarations
struct FermentationStage;

// =====================================================
// FUNÇÕES PRINCIPAIS DE ENVIO
// =====================================================

// Envia estado completo da fermentação para o servidor (30s)
// Inclui: status, etapa, temperaturas, controle (cooling/heating), timeRemaining
void enviarEstadoCompletoMySQL();

// Envia leituras dos sensores para histórico (60s)
// Inclui: temp_fridge, temp_fermenter, temp_target, gravity
void enviarLeiturasSensoresMySQL();

// Envia heartbeat simplificado (saúde do sistema)
// Inclui: uptime, free_heap (sem dados de controle - já em enviarEstadoCompleto)
bool sendHeartbeatMySQL(int configId);

// Envia lista de sensores detectados (sob demanda)
bool sendSensorsDataMySQL(const JsonDocument& doc);

// Envia dados do iSpindel para o MySQL (sob demanda)
bool sendISpindelDataMySQL(const JsonDocument& doc);

// Envia resumo das etapas ao servidor (início de fermentação)
void sendStagesSummaryMySQL();

// =====================================================
// FUNÇÕES AUXILIARES DE FORMATAÇÃO
// =====================================================

// Comprime dados de estado para reduzir tamanho do JSON
void compressStateData(JsonDocument &doc);

// Formata tempo restante no objeto JSON
void formatTimeRemaining(JsonObject& timeRemaining, float remainingH, const char* status);