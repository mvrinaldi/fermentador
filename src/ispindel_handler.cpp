#include "ispindel_handler.h"
#include "ispindel_struct.h"
#include <ArduinoJson.h>

// Definição real da variável global
SpindelData mySpindel;

void setupSpindelRoutes(ESP8266WebServer& server) {
    server.on("/ispindel", HTTP_POST, [&server]() {

        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Body ausente");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "text/plain", "JSON inválido");
            return;
        }

        strlcpy(mySpindel.name, doc["name"] | "iSpindel", sizeof(mySpindel.name));
        mySpindel.temperature = doc["temperature"] | 0.0;
        mySpindel.gravity     = doc["gravity"]     | 0.0;
        mySpindel.battery     = doc["battery"]     | 0.0;
        mySpindel.angle       = doc["angle"]       | 0.0;
        mySpindel.lastUpdate  = millis();
        mySpindel.newDataAvailable = true;

        server.send(200, "text/plain", "OK");
    });
}
