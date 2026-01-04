#pragma once
#include <ESP8266WebServer.h>

// =================================================
// FUNÇÕES PÚBLICAS DO NETWORK MANAGER
// =================================================

/**
 * @brief Inicializa o gerenciador de rede
 * @param server Referência ao servidor web para OTA
 */
void networkSetup(ESP8266WebServer &server);

/**
 * @brief Loop principal do gerenciador de rede
 *        Gerencia WiFi, Firebase, OTA e sensores
 *        Deve ser chamado frequentemente no loop()
 */
void networkLoop();

// =================================================
// FUNÇÕES DE CONSULTA DE ESTADO
// =================================================

/**
 * @brief Verifica se o WiFi está online e estável
 * @return true se WiFi online e com conexão estável
 */
bool isWiFiOnline();

/**
 * @brief Verifica se o Firebase está online e pronto
 * @return true se Firebase autenticado e app.ready() == true
 */
bool isFirebaseOnline();

/**
 * @brief Verifica se o OTA está habilitado e pronto
 * @return true se OTA está configurado e ativo
 */
bool isOTAOnline();

/**
 * @brief Verifica se é seguro usar o Firebase
 * @return true se WiFi e Firebase estão ambos online
 */
bool canUseFirebase();
