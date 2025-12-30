#include "gerenciador_sensores.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h> // CORREÇÃO: Necessário para WiFi.status()

// Referenciando o cliente assíncrono que está no firebase_conexao.cpp
extern AsyncClientClass aClient; 

const char* SENSOR_TYPES[] = {"FERMENTADOR", "GELADEIRA"};
const int NUM_SENSOR_TYPES = 2;

// --- Implementação das Funções ---

String addressToString(DeviceAddress deviceAddress) {
  String str = "";
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) str += "0";
    str += String(deviceAddress[i], HEX);
  }
  str.toUpperCase();
  return str;
}

void scanAndSendSensors() {
  if (WiFi.status() != WL_CONNECTED || !app.ready()) return;

  Serial.println("🔍 Escaneando barramento OneWire...");
  sensors.begin();
  int count = sensors.getDeviceCount();
  
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < count; i++) {
    DeviceAddress tempAddress;
    if (sensors.getAddress(tempAddress, i)) {
      arr.add(addressToString(tempAddress));
    }
  }

  String jsonStr;
  serializeJson(doc, jsonStr);

  Database.set<String>(aClient, "/status/detected_sensors", jsonStr, [](AsyncResult &aResult) {
    if (!aResult.isError()) {
      Serial.println("✅ Lista de sensores detectados enviada!");
    } else {
      Serial.printf("❌ Erro ao enviar scan: %s\n", aResult.error().message().c_str());
    }
  });
}

void setupSensorManager() {
    if (!prefs.begin("ferment_sns", false)) {
        Serial.println("❌ Erro ao abrir Preferences");
    } else {
        Serial.println("✅ Preferences iniciada");
    }
}

bool saveSensorToPreferences(const String& sensorKey, const String& sensorAddress) {
    if (sensorKey.length() == 0) return false;
    return prefs.putString(sensorKey.c_str(), sensorAddress) > 0;
}

bool removeSensorFromPreferences(const String& sensorKey) {
    return prefs.remove(sensorKey.c_str());
}

std::vector<SensorInfo> listSensors() {
    std::vector<SensorInfo> lista;
    const char* chavesPadrao[] = {"sensor_cooler", "sensor_heater", "sensor_aux"};
    
    for (const char* chave : chavesPadrao) {
        String addr = prefs.getString(chave, "");
        if (addr.length() > 0) {
            SensorInfo info;
            strncpy(info.nome, chave, sizeof(info.nome));
            strncpy(info.endereco, addr.c_str(), sizeof(info.endereco));
            lista.push_back(info);
        }
    }
    return lista;
}

bool loadSensorsFromFirebase() {
    Serial.println(F("📥 Buscando configurações de sensores no Firebase..."));
    
    Database.get(aClient, "/config/sensores", [](AsyncResult &aResult) {
        // 1. Verificamos se NÃO houve erro
        if (!aResult.isError()) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, aResult.c_str());
            
            if (!error) {
                JsonObject root = doc.as<JsonObject>();
                // Inicia as preferências para salvar
                prefs.begin("ferment_sns", false); 
                
                for (JsonPair kv : root) {
                    const char* key = kv.key().c_str();
                    String val = kv.value().as<String>();
                    prefs.putString(key, val);
                    Serial.printf("💾 Atualizado localmente: %s -> %s\n", key, val.c_str());
                }
                prefs.end();
                Serial.println(F("✅ Sensores atualizados e salvos no ESP8266"));
            } else {
                Serial.print(F("❌ Erro no Parse JSON: "));
                Serial.println(error.c_str());
            }
        } else {
            Serial.printf("❌ Erro ao buscar do Firebase: %s\n", aResult.error().message().c_str());
        }
    });
    
    return true;
}

void verificarComandoUpdateSensores() {
    static unsigned long ultimaVerificacao = 0;
    
    // Verifica a flag no Firebase a cada 10 segundos
    if (millis() - ultimaVerificacao > 10000) {
        ultimaVerificacao = millis();

        if (WiFi.status() != WL_CONNECTED || !app.ready()) return;

        // Note: Removi o template <String> para evitar ambiguidades
        Database.get(aClient, "/commands/refresh_sensors", [](AsyncResult &aResult) {
            if (!aResult.isError()) {
                
                // CORREÇÃO: Usamos c_str() para pegar o valor bruto e comparar.
                // No Firebase, booleanos retornam "true" ou "false" como texto no c_str()
                String val = aResult.c_str();
                
                if (val == "true") {
                    Serial.println(F("🔄 Comando de atualização recebido!"));
                    
                    // 1. Carrega os novos endereços do Firebase para o Preferences
                    loadSensorsFromFirebase();
                    
                    // 2. Reseta a flag para "false" para evitar repetições infinitas
                    // Usamos Database.set com tipo bool explicitamente
                    Database.set<bool>(aClient, "/commands/refresh_sensors", false, [](AsyncResult &r){
                        if (r.isError()) {
                            Serial.printf("❌ Erro ao resetar flag: %s\n", r.error().message().c_str());
                        } else {
                            Serial.println(F("✅ Flag de comando resetada."));
                        }
                    });
                }
            }
        });
    }
}