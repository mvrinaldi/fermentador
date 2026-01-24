//gerenciador_sensores.cpp
// ✅ REFATORADO: Envio MySQL movido para mysql_sender.cpp
#include "gerenciador_sensores.h"
#include "eeprom_layout.h"
#include <ArduinoJson.h>
#include "network_manager.h"
#include "http_client.h"
#include "mysql_sender.h"  // ✅ NOVO: Módulo de envio MySQL
#include "debug_config.h"

// Cliente HTTP
extern FermentadorHTTPClient httpClient;

// =================================================
// ACESSO AO PONTEIRO DOS SENSORES (PARA BREWPI)
// =================================================

/**
 * Retorna ponteiro para o objeto DallasTemperature global.
 * Usado pelo BrewPi para acesso direto aos sensores.
 * 
 * @return Ponteiro para objeto sensors
 */
DallasTemperature* getSensorsPointer() {
    return &sensors;
}

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
    
    #if DEBUG_SENSORES
    Serial.println(F("✅ EEPROM iniciada (Gerenciador de Sensores)"));
    #endif
    
    // Inicializa biblioteca Dallas
    sensors.begin();
    
    
    #if DEBUG_SENSORES
    int count = sensors.getDeviceCount();
    Serial.printf("[Sensores] %d dispositivo(s) OneWire detectado(s)\n", count);
    #endif
    
    #ifdef DEBUG_EEPROM
    printEEPROMLayout();
    debugEEPROMContents();
    #endif
}

// =================================================
// Scan OneWire
// =================================================

// =================================================
// LIMPAR TODA EEPROM (usar uma vez para corrigir corrupção)
// =================================================
void clearAllSensorsEEPROM() {
    #if DEBUG_SENSORES
    Serial.println(F("🧹 Limpando EEPROM de sensores..."));
    #endif
    
    // Limpa área de sensores
    char empty[SENSOR_ADDR_SIZE] = {0};
    
    EEPROM.put(ADDR_SENSOR_FERMENTADOR, empty);
    EEPROM.put(ADDR_SENSOR_GELADEIRA, empty);
    
    #if DEBUG_SENSORES
    if (EEPROM.commit()) {
        Serial.println(F("✅ EEPROM limpa com sucesso"));
    } else {
        Serial.println(F("❌ Erro ao limpar EEPROM"));
    }
    #else
    EEPROM.commit();
    #endif
}

// =================================================
// VALIDAR SE ENDEREÇO É VÁLIDO (antes de salvar)
// =================================================
bool isValidSensorAddress(const String& address) {
    // Deve ter exatamente 16 caracteres hexadecimais
    if (address.length() != 16) {
        return false;
    }
    
    // Verifica se todos são hex válidos
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

    // ✅ Cria JsonDocument local
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
    
    // Debug opcional: mostra o payload JSON
    #if DEBUG_SENSORES_VERBOSE
    String debugPayload;
    serializeJson(doc, debugPayload);
    Serial.printf("📦 Payload: %s\n", debugPayload.c_str());
    #endif
    #endif
    
    // ✅ REFATORADO: Usa mysql_sender para envio
    sendSensorsDataMySQL(doc);
}

// =================================================
// EEPROM helpers
// =================================================

// =================================================
// SALVAR COM VALIDAÇÃO
// =================================================
bool saveSensorToEEPROM(const char* sensorKey, const String& sensorAddress) {
    // Validação antes de salvar
    if (!isValidSensorAddress(sensorAddress)) {
        #if DEBUG_SENSORES
        Serial.printf("❌ Endereço inválido (não é hex válido): %s\n", sensorAddress.c_str());
        #endif
        return false;
    }
    
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) {
        #if DEBUG_SENSORES
        Serial.printf("❌ Sensor key inválida: %s\n", sensorKey);
        #endif
        return false;
    }

    char buffer[SENSOR_ADDR_SIZE] = {0};
    sensorAddress.toCharArray(buffer, SENSOR_ADDR_SIZE);

    EEPROM.put(addr, buffer);
    bool success = EEPROM.commit();
    
    #if DEBUG_SENSORES
    if (success) {
        Serial.printf("💾 Sensor salvo: %s -> %s (addr %d)\n", 
                     sensorKey, sensorAddress.c_str(), addr);
    } else {
        Serial.printf("❌ Erro ao salvar sensor: %s\n", sensorKey);
    }
    #endif
    
    return success;
}

bool removeSensorFromEEPROM(const char* sensorKey) {
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) return false;

    char empty[SENSOR_ADDR_SIZE] = {0};
    EEPROM.put(addr, empty);
    bool success = EEPROM.commit();
    
    #if DEBUG_SENSORES
    if (success) {
        Serial.printf("🗑️ Sensor removido: %s\n", sensorKey);
    }
    #endif
    
    return success;
}

// =================================================
// LER COM VALIDAÇÃO
// =================================================
String getSensorAddress(const char* sensorKey) {
    int addr = keyToEEPROMAddr(sensorKey);
    if (addr < 0) return "";

    char buffer[SENSOR_ADDR_SIZE];
    EEPROM.get(addr, buffer);

    // Verifica se está vazio ou corrompido
    if (buffer[0] == '\0' || buffer[0] == 0xFF) {
        return "";
    }
    
    // Garante que termina com null
    buffer[SENSOR_ADDR_SIZE - 1] = '\0';
    
    String result = String(buffer);
    
    // Valida se é hex válido
    if (!isValidSensorAddress(result)) {
        #if DEBUG_SENSORES
        Serial.printf("⚠️ Endereço corrompido na EEPROM: %s\n", sensorKey);
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
    // Busca endereços salvos na EEPROM
    String addrFermenterStr = getSensorAddress(SENSOR1_NOME);
    String addrFridgeStr = getSensorAddress(SENSOR2_NOME);
    
    // Verifica se ambos estão configurados
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
    
    // Converte strings para DeviceAddress
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
    
    // Solicita leitura de temperatura
    sensors.requestTemperatures();
    
    // Aguarda conversão (750ms para resolução de 12 bits)
    delay(750);
    
    // Lê temperaturas
    tempFermenter = sensors.getTempC(addrFermenter);
    tempFridge = sensors.getTempC(addrFridge);
    
    // Verifica se as leituras são válidas
    if (tempFermenter == DEVICE_DISCONNECTED_C) {
        #if DEBUG_SENSORES
        Serial.println(F("❌ Erro: Sensor fermentador desconectado"));
        #endif
        return false;
    }
    
    if (tempFridge == DEVICE_DISCONNECTED_C) {
        #if DEBUG_SENSORES
        Serial.println(F("❌ Erro: Sensor geladeira desconectado"));
        #endif
        return false;
    }
    
    // Verifica temperaturas razoáveis (entre -10°C e 50°C)
    if (tempFermenter < -10 || tempFermenter > 50) {
        #if DEBUG_SENSORES
        Serial.printf("⚠️ Temperatura fermentador fora do esperado: %.2f°C\n", tempFermenter);
        #endif
        return false;
    }
    
    if (tempFridge < -10 || tempFridge > 50) {
        #if DEBUG_SENSORES
        Serial.printf("⚠️ Temperatura geladeira fora do esperado: %.2f°C\n", tempFridge);
        #endif
        return false;
    }
    
    // Log periódico (a cada 5 minutos) para não poluir o Serial
    #if DEBUG_SENSORES
    static unsigned long lastLog = 0;
    unsigned long now = millis();
    
    if (now - lastLog >= 300000) {  // 5 minutos
        lastLog = now;
        Serial.printf("🌡️ Fermentador: %.2f°C | Geladeira: %.2f°C\n", tempFermenter, tempFridge);
    }
    #endif
    
    return true;
}