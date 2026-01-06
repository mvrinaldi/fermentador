#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "ispindel_envio.h"
#include "http_client.h"
#include "network_manager.h"

// Cliente HTTP
extern FermentadorHTTPClient httpClient;

// Variáveis de controle de reenvio
const int MAX_TENTATIVAS = 3;
const unsigned long INTERVALO_REENVIO = 30000; // 30 segundos

int tentativasAtuais = 0;
unsigned long últimaTentativaMillis = 0;

void processCloudUpdatesiSpindel() {
    // Verificação de flags e tempo
    if (!mySpindel.newDataAvailable && tentativasAtuais == 0) return;

    if (tentativasAtuais > 0 && (millis() - últimaTentativaMillis < INTERVALO_REENVIO)) {
        return; 
    }

    Serial.printf("[iSpindel] Tentativa %d de envio...\n", tentativasAtuais + 1);

    bool sucesso = false;

    // ==================================================
    // ENVIO 1: BREWFATHER (mantido - serviço externo)
    // ==================================================
    WiFiClientSecure client;
    client.setInsecure(); 
    client.setTimeout(10000); 

    HTTPClient http;
    String url = "https://log.brewfather.net/stream?id=VBmJRwZBAJXDeE";
    
    JsonDocument brewfatherDoc; 
    brewfatherDoc["name"] = mySpindel.name;
    brewfatherDoc["temp"] = mySpindel.temperature;
    brewfatherDoc["gravity"] = mySpindel.gravity;
    brewfatherDoc["battery"] = mySpindel.battery;
    
    String payload;
    serializeJson(brewfatherDoc, payload);

    bool brewfatherOk = false;
    
    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/json");
        int httpCode = http.POST(payload); 
        
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                brewfatherOk = true;
                Serial.printf("[Brewfather] ✅ Sucesso! Código: %d\n", httpCode);
            } else {
                Serial.printf("[Brewfather] ❌ Erro HTTP: %d\n", httpCode);
                if (httpCode == 401 || httpCode == 403 || httpCode == 404) {
                    tentativasAtuais = MAX_TENTATIVAS;
                }
            }
            
            if (!brewfatherOk) {
                String response = http.getString();
                Serial.printf("[Brewfather] Resposta: %s\n", response.c_str());
            }
        } else {
            Serial.printf("[Brewfather] ❌ Erro: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }

    // ==================================================
    // ENVIO 2: MYSQL (nosso banco de dados)
    // ==================================================
    bool mysqlOk = false;
    
    if (isHTTPOnline()) {
        JsonDocument mysqlDoc;
        mysqlDoc["name"] = mySpindel.name;
        mysqlDoc["temperature"] = mySpindel.temperature;
        mysqlDoc["gravity"] = mySpindel.gravity;
        mysqlDoc["battery"] = mySpindel.battery;
        mysqlDoc["angle"] = mySpindel.angle;
        
        String mysqlPayload;
        serializeJson(mysqlDoc, mysqlPayload);
        
        // Envia para endpoint MySQL
        String response;
        HTTPClient httpMySQL;
        WiFiClient wifiClient;
        
        String mysqlUrl = String(API_BASE_URL) + "ispindel.php";
        
        if (httpMySQL.begin(wifiClient, mysqlUrl)) {
            httpMySQL.setTimeout(HTTP_TIMEOUT);
            httpMySQL.addHeader("Content-Type", "application/json");
            
            int code = httpMySQL.POST(mysqlPayload);
            
            if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
                mysqlOk = true;
                Serial.println(F("[MySQL] ✅ Dados iSpindel enviados"));
            } else {
                Serial.printf("[MySQL] ❌ Erro %d\n", code);
            }
            
            httpMySQL.end();
        }
    } else {
        Serial.println(F("[MySQL] ⚠️  Offline - dados não enviados"));
    }

    // Considera sucesso se PELO MENOS UM dos envios funcionou
    sucesso = brewfatherOk || mysqlOk;

    // Lógica de controle de tentativas
    if (sucesso) {
        mySpindel.newDataAvailable = false;
        tentativasAtuais = 0;
        Serial.println(F("[iSpindel] ✅ Envio concluído"));
    } else {
        tentativasAtuais++;
        últimaTentativaMillis = millis();

        if (tentativasAtuais >= MAX_TENTATIVAS) {
            Serial.println(F("[iSpindel] ❌ Máximo de tentativas atingido"));
            mySpindel.newDataAvailable = false; 
            tentativasAtuais = 0;
        }
    }
}