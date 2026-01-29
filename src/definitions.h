// definitions.h - Todas as definições centralizadas
#pragma once

#include <Arduino.h>

// === Definições de Pinos === //
#define PINO_COOLER D5    // GPIO14 - Relé do Aquecedor
#define PINO_HEATER D7    // GPIO13 - Relé do Cooler/Geladeira
#define ONE_WIRE_BUS D6   // GPIO12 - Barramento dos sensores DS18B20

// === Nomes dos Sensores === //
#define SENSOR1_NOME "sensor_fermentador"
#define SENSOR2_NOME "sensor_geladeira"

// === Configurações do Sistema === //
#define EEPROM_SIZE 512                    // Tamanho da EEPROM
#define MAX_STAGES 10                      // Máximo de etapas por fermentação
#define TEMPERATURE_TOLERANCE 0.3f         // Tolerância para considerar temperatura atingida (°C)
#define DEFAULT_TEMPERATURE 5.0f          // Temperatura padrão quando não há fermentação ativa (°C)

// === Intervalos de Tempo (em milissegundos) === //
#define TEMPERATURE_CONTROL_INTERVAL 5000UL   // Controle de temperatura (5s)
#define PHASE_CHECK_INTERVAL 10000UL          // Verificação de troca de fase (10s)
#define ACTIVE_CHECK_INTERVAL 30000UL         // Verificação HTTP (30s)
#define WIFI_CHECK_INTERVAL 60000UL           // Verificação WiFi (60s)

// Intervalo de envio para o banco de dados (5 minutos)
#define READINGS_UPDATE_INTERVAL 300000UL
// Intervalo de envio para o banco de dados (30 segundos para teste)
// #define READINGS_UPDATE_INTERVAL 30000UL

// === Parâmetros PID === //
#define PID_KP 20.0f                       // Ganho proporcional
#define PID_KI 0.5f                        // Ganho integral
#define PID_KD 5.0f                        // Ganho derivativo
#define PID_DEADBAND 0.3f                  // Banda morta (°C)

// === Tempos Mínimos de Ciclo (em milissegundos) === //
#define MIN_COOLER_CYCLE 900000UL          // 15 minutos
#define MIN_HEATER_CYCLE 300000UL          // 5 minutos
#define MIN_COOLER_ON 600000UL    // 10 minutos ← GELADEIRA PRECISA TEMPO!
#define MIN_HEATER_ON 120000UL             // 2 minutos
#define MIN_DELAY_BETWEEN_RELAYS 900000UL  // 15 minutos ← EVITAR CICLOS CURTOS!

// === Constantes de segurança === //
#define MIN_SAFE_TEMPERATURE 0.0f         // Não deixar congelar
#define MAX_SAFE_TEMPERATURE 30.0f        // Limite superior de segurança

#define FERMENTATION_STATE_ENDPOINT "api/esp/state.php"