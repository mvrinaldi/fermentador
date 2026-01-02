// firebase-config.js

export const firebaseConfig = {
    apiKey: "AIzaSyC_qXZvpxyATp_gkxxCnDy3GTtnNFKeDw0",
    authDomain: "fermentador-cedb3.firebaseapp.com",
    databaseURL: "https://fermentador-cedb3-default-rtdb.firebaseio.com",
    projectId: "fermentador-cedb3",
    storageBucket: "fermentador-cedb3.firebasestorage.app",
    messagingSenderId: "590598016188",
    appId: "1:590598016188:web:2ed74ae8f97500cc66d4b9",
    measurementId: "G-THF092WP9J"
};

// Função para inicializar o Firebase
export function initializeFirebase() {
    const firebaseModules = {};
    
    // Carrega dinamicamente os módulos do Firebase
    return {
        async loadModules() {
            // Importa os módulos necessários
            const { initializeApp } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-app.js");
            const { getDatabase } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
            const { getAuth } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js");
            
            // Inicializa o app
            const app = initializeApp(firebaseConfig);
            
            // Retorna os serviços
            return {
                app,
                database: getDatabase(app),
                auth: getAuth(app)
            };
        }
    };
}
