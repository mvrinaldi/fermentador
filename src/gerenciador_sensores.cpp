//gerenciador_sensores.cpp
#include "gerenciador_sensores.h"
#include "preferences_layout.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "network_manager.h"
#include "http_client.h"
#include "mysql_sender.h"
#include "debug_config.h"

// Cliente HTTP
extern FermentadorHTTPClient httpClient;

// ✅ NOVO: Instância Preferences para sensores
Preferences prefsSensors;

// =================================================
// ACESSO AO PONTEIRO DOS SENSORES (PARA BREWPI)
// =================================================

DallasTemperature* getSensorsPointer() {
    return &sensors;
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
    #if DEBUG_SENSORES
    Serial.println(F("✅ Gerenciador de Sensores iniciado (Preferences)"));
    #endif
    
    // Inicializa biblioteca Dallas
    sensors.begin();
    
    #if DEBUG_SENSORES
    int count = sensors.getDeviceCount();
    Serial.printf("[Sensores] %d dispositivo(s) OneWire detectado(s)\n", count);
    #endif
    
    #ifdef DEBUG_EEPROM
    printPreferencesLayout();
    debugPreferencesContents();
    #endif
}

// =================================================
// LIMPAR PREFERENCES DE SENSORES
// =================================================
void clearAllSensorsEEPROM() {
    #if DEBUG_SENSORES
    Serial.println(F("🧹 Limpando namespace sensors..."));
    #endif
    
    clearPreferencesNamespace(PREFS_NAMESPACE_SENSORS);
    
    #if DEBUG_SENSORES
    Serial.println(F("✅ Namespace sensors limpo"));
    #endif
}

// =================================================
// VALIDAR SE ENDEREÇO É VÁLIDO
// =================================================
bool isValidSensorAddress(const String& address) {
    if (address.length() != 16) {
        return false;
    }
    
    String upperAddr = address;
    upperAddr.toUpperCase();
    
    for (int i = 0; i < 16; i++) {
        char c = upperAddr.charAt(i);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    
    return true;
}

void scanAndSendSensors() {
    if (!canUseHTTP()) {
        #if DEBUG_SENSORES
        Serial.println(F("⏸ Scan bloqueado - HTTP offline"));
        #endif
        return;
    }

    #if DEBUG_SENSORES
    Serial.println(F("🔍 Escaneando sensores OneWire..."));
    #endif

    sensors.begin();
    int count = sensors.getDeviceCount();

    if (count == 0) {
        #if DEBUG_SENSORES
        Serial.println(F("⚠️ Nenhum sensor encontrado"));
        #endif
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

    #if DEBUG_SENSORES
    Serial.printf("📡 Enviando %d sensores...\n", arr.size());
    
    #if DEBUG_SENSORES_VERBOSE
    String debugPayload;
    serializeJson(doc, debugPayload);
    Serial.printf("📦 Payload: %s\n", debugPayload.c_str());
    #endif
    #endif
    
    sendSensorsDataMySQL(doc);
}

// =================================================
// SALVAR COM VALIDAÇÃO (PREFERENCES)
// =================================================
bool saveSensorToEEPROM(const char* sensorKey, const String& sensorAddress) {
    if (!isValidSensorAddress(sensorAddress)) {
        #if DEBUG_SENSORES
        Serial.printf("❌ Endereço inválido: %s\n", sensorAddress.c_str());
        #endif
        return false;
    }
    
    const char* prefsKey = nullptr;
    
    if (strcmp(sensorKey, SENSOR1_NOME) == 0) {
        prefsKey = KEY_SENSOR_FERM;
    } else if (strcmp(sensorKey, SENSOR2_NOME) == 0) {
        prefsKey = KEY_SENSOR_FRIDGE;
    } else {
        #if DEBUG_SENSORES
        Serial.printf("❌ Sensor key inválida: %s\n", sensorKey);
        #endif
        return false;
    }

    prefsSensors.begin(PREFS_NAMESPACE_SENSORS, false);
    size_t written = prefsSensors.putString(prefsKey, sensorAddress);
    prefsSensors.end();
    
    bool success = (written > 0);
    
    #if DEBUG_SENSORES
    if (success) {
        Serial.printf("💾 Sensor salvo: %s -> %s\n", 
                     sensorKey, sensorAddress.c_str());
    } else {
        Serial.printf("❌ Erro ao salvar sensor: %s\n", sensorKey);
    }
    #endif
    
    return success;
}

bool removeSensorFromEEPROM(const char* sensorKey) {
    const char* prefsKey = nullptr;
    
    if (strcmp(sensorKey, SENSOR1_NOME) == 0) {
        prefsKey = KEY_SENSOR_FERM;
    } else if (strcmp(sensorKey, SENSOR2_NOME) == 0) {
        prefsKey = KEY_SENSOR_FRIDGE;
    } else {
        return false;
    }

    prefsSensors.begin(PREFS_NAMESPACE_SENSORS, false);
    bool success = prefsSensors.remove(prefsKey);
    prefsSensors.end();
    
    #if DEBUG_SENSORES
    if (success) {
        Serial.printf("🗑️ Sensor removido: %s\n", sensorKey);
    }
    #endif
    
    return success;
}

// =================================================
// LER COM VALIDAÇÃO (PREFERENCES)
// =================================================
String getSensorAddress(const char* sensorKey) {
    const char* prefsKey = nullptr;
    
    if (strcmp(sensorKey, SENSOR1_NOME) == 0) {
        prefsKey = KEY_SENSOR_FERM;
    } else if (strcmp(sensorKey, SENSOR2_NOME) == 0) {
        prefsKey = KEY_SENSOR_FRIDGE;
    } else {
        return "";
    }

    prefsSensors.begin(PREFS_NAMESPACE_SENSORS, true);
    String result = prefsSensors.getString(prefsKey, "");
    prefsSensors.end();
    
    if (result.isEmpty()) {
        return "";
    }
    
    if (!isValidSensorAddress(result)) {
        #if DEBUG_SENSORES
        Serial.printf("⚠️ Endereço corrompido: %s\n", sensorKey);
        #endif
        return "";
    }
    
    return result;
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
// Converte String hexadecimal para DeviceAddress
// =================================================
bool stringToDeviceAddress(const String& str, DeviceAddress addr) {
    if (str.length() != 16) {
        #if DEBUG_SENSORES
        Serial.printf("❌ Endereço inválido (tamanho %d): %s\n", str.length(), str.c_str());
        #endif
        return false;
    }
    
    String upperStr = str;
    upperStr.toUpperCase();
    
    for (uint8_t i = 0; i < 8; i++) {
        String byteStr = upperStr.substring(i * 2, i * 2 + 2);
        char* endPtr;
        long value = strtol(byteStr.c_str(), &endPtr, 16);
        
        if (*endPtr != '\0') {
            #if DEBUG_SENSORES
            Serial.printf("❌ Byte inválido na posição %d: %s\n", i, byteStr.c_str());
            #endif
            return false;
        }
        
        addr[i] = (uint8_t)value;
    }
    
    return true;
}

// =================================================
// Lê temperaturas dos sensores configurados
// =================================================
bool readConfiguredTemperatures(float& tempFermenter, float& tempFridge) {
    String addrFermenterStr = getSensorAddress(SENSOR1_NOME);
    String addrFridgeStr = getSensorAddress(SENSOR2_NOME);
    
    if (addrFermenterStr.isEmpty()) {
        #if DEBUG_SENSORES
        Serial.println(F("⚠️ Sensor fermentador não configurado"));
        #endif
        return false;
    }
    
    if (addrFridgeStr.isEmpty()) {
        #if DEBUG_SENSORES
        Serial.println(F("⚠️ Sensor geladeira não configurado"));
        #endif
        return false;
    }
    
    DeviceAddress addrFermenter, addrFridge;
    
    if (!stringToDeviceAddress(addrFermenterStr, addrFermenter)) {
        #if DEBUG_SENSORES
        Serial.println(F("❌ Erro ao converter endereço fermentador"));
        #endif
        return false;
    }
    
    if (!stringToDeviceAddress(addrFridgeStr, addrFridge)) {
        #if DEBUG_SENSORES
        Serial.println(F("❌ Erro ao converter endereço geladeira"));
        #endif
        return false;
    }
    
    sensors.requestTemperatures();
    delay(750);
    
    tempFermenter = sensors.getTempC(addrFermenter);
    tempFridge = sensors.getTempC(addrFridge);
    
    if (tempFermenter == DEVICE_DISCONNECTED_C) {
        LOG_SENSORES("Erro: Sensor fermentador desconectado");
        if (httpClient.isConnected()) {
            httpClient.sendSensorError(nullptr, 0);
        }
        return false;
    }
    
    if (tempFridge == DEVICE_DISCONNECTED_C) {
        LOG_SENSORES("Erro: Sensor geladeira desconectado");
        if (httpClient.isConnected()) {
            httpClient.sendSensorError(nullptr, 0);
        }
        return false;
    }
    
    if (tempFermenter < -10 || tempFermenter > 50) {
        if (httpClient.isConnected()) {
            httpClient.sendSensorError(nullptr, 0);
        }
        return false;
    }
    
    if (tempFridge < -10 || tempFridge > 50) {
        LOG_SENSORES("Erro: Temperatura geladeira fora do esperado");
        if (httpClient.isConnected()) {
            httpClient.sendSensorError(nullptr, 0);
        }
        return false;
    }
    
    #if DEBUG_SENSORES
    static unsigned long lastLog = 0;
    unsigned long now = millis();
    
    if (now - lastLog >= 300000) {
        lastLog = now;
        Serial.printf("🌡️ Fermentador: %.2f°C | Geladeira: %.2f°C\n", tempFermenter, tempFridge);
    }
    #endif
    
    return true;
}