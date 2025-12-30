#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "ispindel_envio.h"
#include <ArduinoJson.h>

// Variáveis de controle de reenvio
const int MAX_TENTATIVAS = 3;
const unsigned long INTERVALO_REENVIO = 30000; // 30 segundos entre tentativas

int tentativasAtuais = 0;
unsigned long últimaTentativaMillis = 0;

void processCloudUpdates() {
    // Se não há dados novos E não estamos em processo de reenvio, sai fora
    if (!mySpindel.newDataAvailable && tentativasAtuais == 0) return;

    // Se falhou antes, espera o intervalo para tentar de novo
    if (tentativasAtuais > 0 && (millis() - últimaTentativaMillis < INTERVALO_REENVIO)) {
        return; 
    }

    Serial.printf("[Cloud] Tentativa %d de envio...\n", tentativasAtuais + 1);

    WiFiClientSecure client;
    // 1. Adicionar timeout para evitar bloqueios
    client.setTimeout(10000); // 10 segundos
    
    client.setInsecure(); // Necessário para HTTPS no Brewfather
    HTTPClient http;
    
    // Envio para o Brewfather
    String url = "https://log.brewfather.net/stream?id=VBmJRwZBAJXDeE";
    
    String jsonPayload;
    StaticJsonDocument<256> doc;
    doc["name"] = mySpindel.name;
    doc["temp"] = mySpindel.temperature;
    doc["gravity"] = mySpindel.gravity;
    doc["battery"] = mySpindel.battery;
    serializeJson(doc, jsonPayload);

    bool sucesso = false;

    if (http.begin(client, url)) {
        // 2. Adicionar headers (opcional, mas recomendado)
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        
        int httpCode = http.POST(jsonPayload);
        
        if (httpCode > 0) {
            // 3. Verificar código de resposta específico
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                sucesso = true;
                Serial.printf("[Brewfather] Sucesso! Código: %d\n", httpCode);
            } else {
                Serial.printf("[Brewfather] Erro HTTP: %d\n", httpCode);
                if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == HTTP_CODE_NOT_FOUND) {
                    // ID incorreto ou sem permissão - não adianta retentar
                    tentativasAtuais = MAX_TENTATIVAS;
                }
            }
            
            // 4. Melhor tratamento de erro
            String response = http.getString();
            if (response.length() > 0) {
                Serial.printf("[Brewfather] Resposta: %s\n", response.c_str());
            }
        } else {
            // Erro de conexão (httpCode < 0)
            Serial.printf("[Brewfather] Erro de conexão: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }

    if (sucesso) {
        // Tudo certo, limpa as flags
        mySpindel.newDataAvailable = false;
        tentativasAtuais = 0;
    } else {
        // Falhou, incrementa tentativa e marca o tempo
        tentativasAtuais++;
        últimaTentativaMillis = millis();

        if (tentativasAtuais >= MAX_TENTATIVAS) {
            Serial.println("[Cloud] Máximo de tentativas atingido. Desistindo desta leitura.");
            mySpindel.newDataAvailable = false; // Desiste para não travar o loop para sempre
            tentativasAtuais = 0;
        }
    }
}

// Lógica para envio ao Firebase pode ser adicionada aqui de forma similar