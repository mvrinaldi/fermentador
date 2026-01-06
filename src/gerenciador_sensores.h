#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <DallasTemperature.h>
#include <vector>

#include "estruturas.h"
#include "definitions.h"

// Referência ao barramento OneWire já criado em globais.cpp
extern DallasTemperature sensors;

// --- Inicialização ---
void setupSensorManager();

// --- Scan ---
void scanAndSendSensors();
String addressToString(DeviceAddress deviceAddress);

// --- Dados ---
std::vector<SensorInfo> listSensors();

// --- EEPROM ---
bool saveSensorToEEPROM(const char* sensorKey, const String& sensorAddress);
bool removeSensorFromEEPROM(const char* sensorKey);
String getSensorAddress(const char* sensorKey);