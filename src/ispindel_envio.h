#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "ispindel_struct.h"
#include <ArduinoJson.h>

void processCloudUpdates() {
    if (!mySpindel.newDataAvailable) return;

    // 1. Lógica Brewfather (HTTP POST)
    WiFiClientSecure client;
    client.setInsecure(); // Brewfather usa HTTPS
    HTTPClient http;
    
    // URL do seu Brewfather
    String url = "http://log.brewfather.net/stream?id=VBmJRwZBAJXDeE";
    
    // O Brewfather aceita o mesmo formato JSON que o iSpindel envia
    String jsonPayload;
    StaticJsonDocument<256> doc;
    doc["name"] = mySpindel.name;
    doc["temp"] = mySpindel.temperature; // Brewfather espera "temp"
    doc["gravity"] = mySpindel.gravity;
    doc["battery"] = mySpindel.battery;
    serializeJson(doc, jsonPayload);

    if (http.begin(client, url)) {
        int httpCode = http.POST(jsonPayload);
        Serial.printf("[Brewfather] Código: %d\n", httpCode);
        http.end();
    }

    // 2. Lógica Firebase (Exemplo simplificado)
    // Aqui você usaria a biblioteca Firebase-ESP-Client ou similar
    // Firebase.RTDB.setFloat(&fbdo, "fermentacao/gravidade", mySpindel.gravity);

    mySpindel.newDataAvailable = false; // Reset da flag após enviar tudo
}