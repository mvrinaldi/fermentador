// telnet.cpp - Implementação do servidor Telnet para debug remoto
#include "telnet.h"
#include <ESP8266WiFi.h>
#include "debug_config.h"

// ========================
// CONFIG
// ========================
#if DEBUG_TELNET

WiFiServer telnetServer(23);
WiFiClient telnetClient;

void telnetSetup() {
    telnetServer.begin();
    telnetServer.setNoDelay(true);
}

void telnetLoop() {
    if (telnetServer.hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            telnetClient = telnetServer.accept();
            telnetClient.println();
            telnetClient.println(F("==================================="));
            telnetClient.println(F("  ESP8266 Debug Telnet conectado"));
            telnetClient.println(F("==================================="));
            telnetClient.println();
        } else {
            // Já tem cliente conectado, rejeita novos
            WiFiClient newClient = telnetServer.accept();
            newClient.stop();
        }
    }
}

void telnetLog(const String &msg) {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(msg);
    }
}

bool isTelnetConnected() {
    return telnetClient && telnetClient.connected();
}

#else

// ========================
// STUBS (quando desligado)
// ========================

void telnetSetup() {}
void telnetLoop() {}
void telnetLog(const String &msg) { (void)msg; }
bool isTelnetConnected() { return false; }

#endif
