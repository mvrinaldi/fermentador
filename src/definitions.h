#pragma once

#include <Arduino.h>

// === Definições de Pinos === //
#define PINO_COOLER D7    // GPIO14 - Relé do Cooler/Geladeira
#define PINO_HEATER D5    // GPIO13 - Relé do Aquecedor
#define ONE_WIRE_BUS D6   // GPIO12 - Barramento dos sensores DS18B20

// === Nomes dos Sensores === //
#define SENSOR1_NOME "sensor_fermentador"
#define SENSOR2_NOME "sensor_geladeira"

// === Configurações do Sistema === //
#define EEPROM_SIZE 512                    // Tamanho da EEPROM
#define MAX_STAGES 10                      // Máximo de etapas por fermentação
#define TEMPERATURE_TOLERANCE 0.5          // Tolerância para considerar temperatura atingida (°C)

// === Intervalos de Tempo (em milissegundos) === //
#define TEMPERATURE_CONTROL_INTERVAL 5000  // Controle de temperatura (5s)
#define PHASE_CHECK_INTERVAL 10000         // Verificação de troca de fase (10s)
#define ACTIVE_CHECK_INTERVAL 30000        // Verificação Firebase (30s)
#define WIFI_CHECK_INTERVAL 60000          // Verificação WiFi (60s)

// === Parâmetros PID === //
#define PID_KP 20.0                        // Ganho proporcional
#define PID_KI 0.5                         // Ganho integral
#define PID_KD 5.0                         // Ganho derivativo
#define PID_DEADBAND 0.3                   // Banda morta (°C)

// === Tempos Mínimos de Ciclo (em milissegundos) === //
#define MIN_COOLER_CYCLE 900000            // 15 minutos
#define MIN_HEATER_CYCLE 300000            // 5 minutos
#define MIN_COOLER_ON 180000               // 3 minutos
#define MIN_HEATER_ON 120000               // 2 minutos
#define MIN_DELAY_BETWEEN_RELAYS 60000     // 1 minuto entre acionamentos