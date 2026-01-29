//ispindel_handler.h
#pragma once

#include <ESP8266WebServer.h>

// Inicializa as rotas do iSpindel no servidor fornecido
void setupSpindelRoutes(ESP8266WebServer& server);
void sendImmediateToMySQL();