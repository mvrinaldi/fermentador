#include "gerenciador_sensores.h"
#include "eeprom_layout.h"  // ← NOVO: Layout unificado
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

// Firebase async client
extern AsyncClientClass aClient;

// =================================================
// MAPEAMENTO DE SENSORES → EEPROM
// =================================================
// Agora usando endereços do eeprom_layout.h

int keyToEEPROMAddr(const char* key) {
    if (strcmp(key, SENSOR1_NOME) == 0) return ADDR_SENSOR_FERMENTADOR;
    if (strcmp(key, SENSOR2_NOME) == 0) return ADDR_SENSOR_GELADEIRA;
    return -1;
}

// =================================================
// Utils
// =================================================

String addressToString(DeviceAddress deviceAddress) {
    char buffer[17];
    for (uint8_t i = 0; i < 8; i++) {
        sprintf(&buffer[i * 2], "%02X", deviceAddress[i]);
    }
    buffer[16] = '\0';
    return String(buffer);
}

// =================================================
// Inicialização
// =================================================

void setupSensorManager() {
    EEPROM.begin(EEPROM_SIZE); // Usa EEPROM_SIZE do layout unificado (512)
    Serial.println(F("✅ EEPROM iniciada (Gerenciador de Sensores)"));
    
    // Debug opcional do layout
    #ifdef DEBUG_EEPROM
    printEEPROMLayout();
    debugEEPROMContents();
    #endif
}

// =================================================
// Scan OneWire
// =================================================

void scanAndSendSensors() {
    if (WiFi.status() != WL_CONNECTED || !app.ready()) return;

    Serial.println(F("🔍 Escaneando sensores OneWire..."));

    sensors.begin();
    int count = sensors.getDeviceCount();

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < count; i++) {
        DeviceAddress addr;
        if (sensors.getAddress(addr, i)) {
            arr.add(addressToString(addr));
        }
    }

    String payload;
    serializeJson(doc, payload);

    Database.set<String>(
        aClient,
        "/status/detected_sensors",
        payload,
        [](AsyncResult &r) {
            if (r.isError()) {
                Serial.printf("❌ Erro scan: %s\n", r.error().message().c_str());
            } else {
                Serial.println(F("✅ Scan enviado ao Firebase"));
            }
        }
    );
}

// =================================================
// EEPROM helpers - SEÇÃO DE SENSORES (0-63)
// =================================================

bool saveSensorToEEPROM(const char* sensorKey, const String& sensorAddress) {
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) {
        Serial.printf("❌ Sensor key inválida: %s\n", sensorKey);
        return false;
    }

    char buffer[SENSOR_ADDR_SIZE] = {0};
    sensorAddress.toCharArray(buffer, SENSOR_ADDR_SIZE);

    EEPROM.put(addr, buffer);
    bool success = EEPROM.commit();
    
    if (success) {
        Serial.printf("💾 Sensor salvo na EEPROM: %s -> %s (addr %d)\n", 
                     sensorKey, sensorAddress.c_str(), addr);
    } else {
        Serial.printf("❌ Erro ao salvar sensor: %s\n", sensorKey);
    }
    
    return success;
}

bool removeSensorFromEEPROM(const char* sensorKey) {
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) return false;

    char empty[SENSOR_ADDR_SIZE] = {0};
    EEPROM.put(addr, empty);
    bool success = EEPROM.commit();
    
    if (success) {
        Serial.printf("🗑️ Sensor removido da EEPROM: %s\n", sensorKey);
    }
    
    return success;
}

String getSensorAddress(const char* sensorKey) {
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) return "";

    char buffer[SENSOR_ADDR_SIZE];
    EEPROM.get(addr, buffer);

    if (buffer[0] == '\0') return "";
    return String(buffer);
}

// =================================================
// Lista sensores configurados
// =================================================

std::vector<SensorInfo> listSensors() {
    std::vector<SensorInfo> lista;

    const char* keys[] = {SENSOR1_NOME, SENSOR2_NOME};

    for (const char* key : keys) {
        String addr = getSensorAddress(key);
        if (addr.length() > 0) {
            SensorInfo s;
            strncpy(s.nome, key, sizeof(s.nome));
            strncpy(s.endereco, addr.c_str(), sizeof(s.endereco));
            lista.push_back(s);
        }
    }

    return lista;
}

// =================================================
// Firebase → EEPROM
// =================================================

bool loadSensorsFromFirebase() {
    Serial.println(F("📥 Buscando sensores no Firebase..."));

    Database.get(
        aClient,
        "/config/sensores",
        [](AsyncResult &r) {

            if (r.isError()) {
                Serial.printf("❌ Firebase erro: %s\n", r.error().message().c_str());
                return;
            }

            JsonDocument doc;
            if (deserializeJson(doc, r.c_str())) {
                Serial.println(F("❌ Erro parse JSON sensores"));
                return;
            }

            JsonObject root = doc.as<JsonObject>();

            for (JsonPair kv : root) {
                saveSensorToEEPROM(
                    kv.key().c_str(),
                    kv.value().as<String>()
                );

                Serial.printf(
                    "💾 EEPROM: %s -> %s\n",
                    kv.key().c_str(),
                    kv.value().as<const char*>()
                );
            }

            Serial.println(F("✅ Sensores atualizados"));
        }
    );

    return true;
}

// =================================================
// Comando remoto de refresh
// =================================================

void verificarComandoUpdateSensores() {
    static unsigned long ultima = 0;
    if (millis() - ultima < 10000) return;
    ultima = millis();

    if (WiFi.status() != WL_CONNECTED || !app.ready()) return;

    Database.get(
        aClient,
        "/commands/refresh_sensors",
        [](AsyncResult &r) {
            if (r.isError()) return;

            if (String(r.c_str()) == "true") {
                Serial.println(F("🔄 Comando refresh recebido"));

                loadSensorsFromFirebase();

                Database.set<bool>(
                    aClient,
                    "/commands/refresh_sensors",
                    false,
                    [](AsyncResult &res) {
                        if (!res.isError()) {
                            Serial.println(F("✅ Flag resetada"));
                        }
                    }
                );
            }
        }
    );
}