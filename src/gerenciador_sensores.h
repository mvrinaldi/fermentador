#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <DallasTemperature.h>
#include <vector>
#include "estruturas.h"
#include "firebase_conexao.h"

// Referências externas
extern Preferences prefs; 
extern DallasTemperature sensors;

// --- Funções Principais de Inicialização e Loop ---
void setupSensorManager();
void verificarComandoUpdateSensores();

// --- Funções de Hardware ---
void scanAndSendSensors(); // Adicionado: Essencial para o scan aparecer no HTML
String addressToString(DeviceAddress deviceAddress);

// --- Funções de Dados e Firebase ---
std::vector<SensorInfo> listSensors();
bool loadSensorsFromFirebase();

// --- Helpers de Armazenamento Local (Preferences) ---
bool saveSensorToPreferences(const String& sensorKey, const String& sensorAddress);
bool removeSensorFromPreferences(const String& sensorKey);
String getSensorAddress(const String& sensorKey);
