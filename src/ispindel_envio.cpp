#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FirebaseClient.h> 
#include "ispindel_envio.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "firebase_conexao.h"
AsyncResult result;

// Variáveis de controle de reenvio
const int MAX_TENTATIVAS = 3;
const unsigned long INTERVALO_REENVIO = 30000; // 30 segundos entre tentativas

int tentativasAtuais = 0;
unsigned long últimaTentativaMillis = 0;

void processCloudUpdatesiSpindel() {
    // 1. Verificação de flags e tempo (Ok)
    if (!mySpindel.newDataAvailable && tentativasAtuais == 0) return;

    if (tentativasAtuais > 0 && (millis() - últimaTentativaMillis < INTERVALO_REENVIO)) {
        return; 
    }

    Serial.printf("[Cloud] Tentativa %d de envio...\n", tentativasAtuais + 1);

    // WiFiClientSecure e HTTPClient podem ser static para economizar recursos
    WiFiClientSecure client;
    client.setInsecure(); 
    client.setTimeout(10000); 

    HTTPClient http;
    String url = "https://log.brewfather.net/stream?id=VBmJRwZBAJXDeE";
    
    // Ajuste de tamanho do JsonDocument conforme a biblioteca ArduinoJson 6+
    JsonDocument doc; 
    doc["name"] = mySpindel.name;
    doc["temp"] = mySpindel.temperature;
    doc["gravity"] = mySpindel.gravity;
    doc["battery"] = mySpindel.battery;
    
    String payload;
    serializeJson(doc, payload);

    bool sucesso = false;

    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/json");
        
        // CORREÇÃO: payload em minúsculo conforme declarado
        int httpCode = http.POST(payload); 
        
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                sucesso = true;
                Serial.printf("[Brewfather] Sucesso! Código: %d\n", httpCode);
            } else {
                Serial.printf("[Brewfather] Erro HTTP: %d\n", httpCode);
                if (httpCode == 401 || httpCode == 403 || httpCode == 404) {
                    tentativasAtuais = MAX_TENTATIVAS; // Erro fatal de auth/url
                }
            }
            
            // Opcional: só ler a resposta se não for sucesso para depurar
            if (!sucesso) {
                String response = http.getString();
                Serial.printf("[Brewfather] Resposta Erro: %s\n", response.c_str());
            }
        } else {
            Serial.printf("[Brewfather] Erro de conexão: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end(); // Importante para liberar o socket
    }

    // Lógica de controle de tentativas (Ok)
    if (sucesso) {
        mySpindel.newDataAvailable = false;
        tentativasAtuais = 0;
    } else {
        tentativasAtuais++;
        últimaTentativaMillis = millis();

        if (tentativasAtuais >= MAX_TENTATIVAS) {
            Serial.println("[Cloud] Máximo de tentativas atingido. Desistindo.");
            mySpindel.newDataAvailable = false; 
            tentativasAtuais = 0;
        }
    }
}
