#pragma once

#define ENABLE_DATABASE
#define ENABLE_USER_AUTH

#include <FirebaseClient.h>
#include "globais.h"

// Objetos globais do Firebase
extern FirebaseApp app;
extern RealtimeDatabase Database;
extern AsyncClientClass aClient;

// Inicialização
void setupFirebase();
