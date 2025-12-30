#include "ispindel_handler.h"
#include "ispindel_struct.h"
#include <ArduinoJson.h>

// Definição real da variável global declarada no .h
SpindelData mySpindel;

void setupSpindelRoutes(AsyncWebServer& server) {
    server.on("/ispindel", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            
            strlcpy(mySpindel.name, doc["name"] | "iSpindel", sizeof(mySpindel.name));
            mySpindel.temperature = doc["temperature"];
            mySpindel.gravity     = doc["gravity"];
            mySpindel.battery     = doc["battery"];
            mySpindel.angle       = doc["angle"];
            mySpindel.lastUpdate  = millis();
            mySpindel.newDataAvailable = true; // Sinaliza que há novos dados para Firebase/Brewfather

            Serial.println("Dados do iSpindel atualizados na Struct!");
        }
    });
}