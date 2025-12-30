#include "firebase_conexao.h"
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

// ================= AUTENTICAÇÃO =================
UserAuth user_auth(FIREBASE_API_KEY, USER_EMAIL, USER_PASS);

// ================= OBJETOS FIREBASE ==============
FirebaseApp app;
WiFiClientSecure ssl_client;
AsyncClientClass aClient(ssl_client);
RealtimeDatabase Database;

// ================= SSL ===========================
static void configurarCertificadoSSL(WiFiClientSecure& client) {
    // ESP8266: evita consumo excessivo de RAM
    client.setInsecure();
    client.setTimeout(2000);
}

// ================= SETUP FIREBASE ================
void setupFirebase() {
    configurarCertificadoSSL(ssl_client);

    Serial.println(F("🔐 Inicializando Firebase Auth..."));

    initializeApp(
        aClient,
        app,
        getAuth(user_auth),

        // 🔥 CALLBACK COM LAMBDA
        [](AsyncResult &aResult) {
            if (aResult.isError()) {
                Serial.printf(
                    "❌ Firebase Auth erro: %s\n",
                    aResult.error().message().c_str()
                );
                useFirebase = false;
            } else {
                Serial.println(F("✅ Firebase autenticado com sucesso"));
                useFirebase = true;
            }
        },

        "authTask"
    );

    // ================= DATABASE ==================
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);

    Serial.println(F("📡 Firebase Realtime Database configurado"));
}
