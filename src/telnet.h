#pragma once
#include <Arduino.h>

void telnetSetup();
void telnetLoop();
void telnetLog(const String &msg);
bool isTelnetConnected();

// telnet.h - para debug sem precisar conectar esp via usb
// para acessar:
// Windows + R
// Digite: cmd
// Digite: telnet 192.168.68.108
// MELHOR: Instale https://www.putty.org 
// Abra o PuTTY
// HostName (or IP Adress): O do esp que neste é    
// Clique em "Other" e na caixa "Telnet"
// Clique em "Open" e vai abrir o programa e mostrar os logs

// Para usar:
// Troque: Serial.println("Falha na sincronização");
// Por: LOG_definido em debug_config("Falha na sincronização");