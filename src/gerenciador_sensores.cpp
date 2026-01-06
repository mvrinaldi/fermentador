#include "gerenciador_sensores.h"
#include "eeprom_layout.h"
#include <ArduinoJson.h>
#include "network_manager.h"
#include "http_client.h"

// Cliente HTTP
extern FermentadorHTTPClient httpClient;

// =================================================
// MAPEAMENTO DE SENSORES → EEPROM
// =================================================

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
    EEPROM.begin(EEPROM_SIZE);
    Serial.println(F("✅ EEPROM iniciada (Gerenciador de Sensores)"));
    
    #ifdef DEBUG_EEPROM
    printEEPROMLayout();
    debugEEPROMContents();
    #endif
}

// =================================================
// Scan OneWire
// =================================================

void scanAndSendSensors() {
    if (!canUseHTTP()) {
        Serial.println(F("⏸ Scan bloqueado - HTTP offline"));
        return;
    }

    Serial.println(F("🔍 Escaneando sensores OneWire..."));

    sensors.begin();
    int count = sensors.getDeviceCount();

    if (count == 0) {
        Serial.println(F("⚠️ Nenhum sensor encontrado"));
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc["sensors"].to<JsonArray>();

    for (int i = 0; i < count; i++) {
        DeviceAddress addr;
        if (sensors.getAddress(addr, i)) {
            arr.add(addressToString(addr));
        }
    }

    Serial.printf("📡 Enviando %d sensores...\n", arr.size());

    String payload;
    serializeJson(doc, payload);

    if (httpClient.sendSensors(payload)) {
        Serial.println(F("✅ Sensores enviados"));
    } else {
        Serial.println(F("❌ Erro ao enviar sensores"));
    }
}

// =================================================
// EEPROM helpers
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
        Serial.printf("💾 Sensor salvo: %s -> %s (addr %d)\n", 
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
        Serial.printf("🗑️ Sensor removido: %s\n", sensorKey);
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
// Comando remoto de refresh (removido - não aplicável)
// =================================================

void verificarComandoUpdateSensores() {
    // Esta função era específica do Firebase
    // Com MySQL, o scan é feito localmente ou via comando direto
    // Pode ser removida ou adaptada para polling HTTP se necessário
}