#include <ArduinoJson.h>

#include "ispindel_handler.h"
#include "ispindel_struct.h"
#include "debug_config.h"

// Definição real da variável global
SpindelData mySpindel;

void setupSpindelRoutes(ESP8266WebServer& server) {
    LOG_ISPINDEL("[iSpindel] Rotas do iSpindel inicializadas");
    
    server.on("/gravity", HTTP_POST, [&server]() {
        // Log 1: Quando a requisição é recebida
        LOG_ISPINDEL("[iSpindel] 📥 Requisição POST recebida em /gravity");

        if (!server.hasArg("plain")) {
            LOG_ISPINDEL("[iSpindel] Body ausente na requisição");
            server.send(400, "text/plain", "Body ausente");
            return;
        }

        // Log 2: Mostrar os dados brutos recebidos
        String rawData = server.arg("plain");
        LOG_ISPINDEL("[iSpindel] Dados brutos recebidos: " + rawData);

        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            LOG_ISPINDEL("[iSpindel] Falha ao decodificar JSON");
            server.send(400, "text/plain", "JSON inválido");
            return;
        }

        // Extrair valores
        const char* name = doc["name"] | "iSpindel";
        float temperature = doc["temperature"] | 0.0;
        float gravity = doc["gravity"] | 0.0;
        float battery = doc["battery"] | 0.0;
        float angle = doc["angle"] | 0.0;

        // Log 3: Mostrar todos os dados recebidos
        LOG_ISPINDEL("[iSpindel] Dados recebidos - " +
            String(name) + ": " +
            "Temp: " + String(temperature, 2) + "°C, " +
            "Gravity: " + String(gravity, 4) + ", " +
            "Battery: " + String(battery, 2) + "V, " +
            "Angle: " + String(angle, 1) + "°");

        // Atribuir aos valores da estrutura
        strlcpy(mySpindel.name, name, sizeof(mySpindel.name));
        mySpindel.temperature = temperature;
        mySpindel.gravity     = gravity;
        mySpindel.battery     = battery;
        mySpindel.angle       = angle;
        mySpindel.lastUpdate  = millis();
        mySpindel.newDataAvailable = true;

        // Log 4: Confirmação de processamento
        LOG_ISPINDEL("[iSpindel] Dados processados e armazenados");

        server.send(200, "text/plain", "OK");
    });
}