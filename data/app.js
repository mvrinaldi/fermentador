// app.js - Monitor de Fermentação com Sistema de Rampas
import { firebaseConfig } from './firebase-config.js';

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;
let database, auth;

// ========== CONTROLE DE LIMPEZA ==========
let lastCleanupTime = 0;
const CLEANUP_INTERVAL = 6 * 60 * 60 * 1000; // 6 horas entre limpezas
const MAX_READINGS_PER_LOAD = 500; // Máximo de leituras mantidas por carga

// ========== CONSTANTES ==========
const TEMP_TOLERANCE = 0.5; // ±0.5°C de tolerância
const CHECK_INTERVAL = 5 * 60 * 1000; // Verificar a cada 5 minutos

// ========== ESTADO DA APLICAÇÃO ==========
let state = {
    config: null,
    readings: [],
    latestReading: null,
    rampProgress: null,
    currentTargetTemp: null,
    stageTimers: {} // Para rastrear quando cada etapa começou a contar
};

// ========== INICIALIZAÇÃO DO FIREBASE ==========
async function initializeFirebase() {
    try {
        const { initializeApp } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-app.js");
        const { getDatabase } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
        const { getAuth } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js");
        
        const app = initializeApp(firebaseConfig);
        database = getDatabase(app);
        auth = getAuth(app);
        console.log('Firebase inicializado com sucesso no app.js');
        return true;
    } catch (error) {
        console.error('Erro ao inicializar Firebase:', error);
        return false;
    }
}

// ========== FUNÇÕES AUXILIARES DO FIREBASE ==========
function requireFirebaseDatabase() {
    return {
        ref: (...args) =>
            import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js")
                .then(({ ref }) => ref(database, ...args)),

        get: async (ref) => {
            const { get } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
            return get(ref);
        },

        set: async (ref, data) => {
            const { set } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
            return set(ref, data);
        },

        update: async (ref, data) => {
            const { update } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
            return update(ref, data);
        },

        onValue: (ref, callback) => {
            import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js")
                .then(({ onValue }) => onValue(ref, callback));
        },

        remove: async (ref) => {
            const { remove } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");
            return remove(ref);
        },

        // FUNÇÃO DE LIMPEZA CORRIGIDA
        cleanOldReadings: async (configId) => {
            const now = Date.now();
            
            // Controle de frequência: só limpa a cada 6 horas
            if (now - lastCleanupTime < CLEANUP_INTERVAL) {
                console.log('Limpeza ignorada: ainda dentro do intervalo de 6 horas');
                return;
            }
            
            try {
                const {
                    ref,
                    query,
                    orderByChild,
                    startAt,
                    endAt,
                    get,
                    remove
                } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");

                const thirtyDaysAgo = Date.now() - (30 * 24 * 60 * 60 * 1000);

                const readingsRef = ref(database, `readings/${configId}`);
                
                // QUERY CORRIGIDA: pega dados ANTIGOS (antes de 30 dias)
                const oldDataQuery = query(
                    readingsRef,
                    orderByChild("timestamp"),
                    startAt(0),           // Começa do timestamp 0
                    endAt(thirtyDaysAgo)  // Termina em 30 dias atrás
                );

                const snapshot = await get(oldDataQuery);

                if (!snapshot.exists()) {
                    console.log('Nenhum dado antigo para limpar (30+ dias)');
                    lastCleanupTime = now;
                    return;
                }

                // Limpeza em lotes para evitar timeout
                const BATCH_SIZE = 100;
                const deletePromises = [];
                let count = 0;

                snapshot.forEach(child => {
                    if (count < BATCH_SIZE) {
                        deletePromises.push(remove(child.ref));
                        count++;
                    }
                });

                await Promise.all(deletePromises);
                console.log(`Limpeza de dados antigos: ${count} registros removidos (30+ dias)`);
                
                // Atualiza o tempo da última limpeza
                lastCleanupTime = now;
                
                // Retorna o número de registros removidos para possível logging
                return count;
                
            } catch (error) {
                console.error('Erro durante a limpeza de dados antigos:', error);
                throw error;
            }
        },

        // NOVA FUNÇÃO: Limpeza leve para manter performance
        optimizeReadings: async (configId) => {
            try {
                const {
                    ref,
                    query,
                    orderByChild,
                    limitToFirst,
                    get,
                    remove
                } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-database.js");

                const readingsRef = ref(database, `readings/${configId}`);
                
                // Pega as leituras mais antigas, mantendo apenas as mais recentes
                const optimizationQuery = query(
                    readingsRef,
                    orderByChild("timestamp"),
                    limitToFirst(MAX_READINGS_PER_LOAD + 100) // Mantém buffer
                );

                const snapshot = await get(optimizationQuery);

                if (!snapshot.exists() || snapshot.size <= MAX_READINGS_PER_LOAD) {
                    return;
                }

                // Calcula quantas remover
                const toRemove = snapshot.size - MAX_READINGS_PER_LOAD;
                const deletePromises = [];
                let removedCount = 0;

                snapshot.forEach(child => {
                    if (removedCount < toRemove) {
                        deletePromises.push(remove(child.ref));
                        removedCount++;
                    }
                });

                if (removedCount > 0) {
                    await Promise.all(deletePromises);
                    console.log(`Otimização: ${removedCount} registros antigos removidos (mantendo ${MAX_READINGS_PER_LOAD} mais recentes)`);
                }
                
            } catch (error) {
                console.error('Erro durante otimização de leituras:', error);
            }
        }
    };
}

// ========== FUNÇÕES DE AUTENTICAÇÃO ==========
async function login() {
    const email = document.getElementById('login-email').value;
    const password = document.getElementById('login-password').value;
    
    if (!email || !password) {
        showError('Por favor, preencha email e senha');
        return;
    }
    
    try {
        const { signInWithEmailAndPassword } = await import(
            "https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js"
        );
        
        await signInWithEmailAndPassword(auth, email, password);
        hideError();
    } catch (error) {
        console.error('Erro no login:', error.code, error.message);
        let message = 'Erro no login';
        switch (error.code) {
            case 'auth/invalid-email': message = 'Email inválido'; break;
            case 'auth/user-disabled': message = 'Usuário desativado'; break;
            case 'auth/user-not-found': message = 'Usuário não encontrado'; break;
            case 'auth/wrong-password': message = 'Senha incorreta'; break;
            case 'auth/too-many-requests': message = 'Muitas tentativas. Tente mais tarde'; break;
            default: message = error.message;
        }
        showError(message);
    }
}

function resetLoginButton() {
    const loginBtn = document.getElementById('login-btn');
    if (loginBtn) {
        loginBtn.innerHTML = '<i class="fas fa-sign-in-alt"></i> Entrar';
        loginBtn.disabled = false;
    }
}

function showError(message) {
    const errorDiv = document.getElementById('login-error');
    const errorMessage = document.getElementById('error-message');
    
    if (errorDiv && errorMessage) {
        errorMessage.textContent = message;
        errorDiv.classList.remove('hidden');
        errorDiv.classList.add('flex', 'items-center', 'gap-2');
    } else {
        alert(message);
    }
}

function hideError() {
    const errorDiv = document.getElementById('login-error');
    if (errorDiv) {
        errorDiv.classList.add('hidden');
        errorDiv.classList.remove('flex');
    }
}

function showLoginScreen() {
    const loginScreen = document.getElementById('login-screen');
    const container = document.querySelector('.container');
    
    if (loginScreen) {
        loginScreen.style.display = 'flex';
    }
    if (container) {
        container.style.display = 'none';
    }
    
    resetLoginButton();
}

function hideLoginScreen() {
    const loginScreen = document.getElementById('login-screen');
    const container = document.querySelector('.container');
    
    if (loginScreen) {
        loginScreen.style.display = 'none';
    }
    if (container) {
        container.style.display = 'block';
    }
    
    resetLoginButton();
}

async function logout() {
    try {
        const { signOut } = await import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js");
        await signOut(auth);
        console.log('Usuário deslogado');
        
        // 🔥 LIMPAR TEMPORIZADORES AO FAZER LOGOUT
        state.stageTimers = {};
        localStorage.removeItem('stageTimers');
        console.log('🧹 Temporizadores limpos ao fazer logout');
    } catch (error) {
        console.error('Erro ao fazer logout:', error);
        alert('Erro ao fazer logout: ' + error.message);
    }
}

function requireFirebaseAuth() {
    return {
        onAuthStateChanged: (auth, callback) => {
            import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js")
                .then(({ onAuthStateChanged }) => onAuthStateChanged(auth, callback));
        },
        signInWithEmailAndPassword: (auth, email, password) => {
            return import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js")
                .then(({ signInWithEmailAndPassword }) => signInWithEmailAndPassword(auth, email, password));
        },
        signOut: () => auth.signOut()
    };
}

// ========== NOVAS FUNÇÕES PARA CONTROLE DE TEMPERATURA ==========

/**
 * Verifica se a temperatura atual está dentro da faixa alvo
 */
function isTemperatureOnTarget(currentTemp, targetTemp) {
    if (!currentTemp || !targetTemp) return false;
    return Math.abs(currentTemp - targetTemp) <= TEMP_TOLERANCE;
}

/**
 * Obtém o tempo real decorrido para uma etapa
 */
function getActualElapsedTime(stage, stageIndex) {
    if (!stage || !stage.startTime) return 0;
    
    const stageKey = `${state.config?.id}_${stageIndex}`;
    
    // Para rampas, o tempo conta desde o início independente da temperatura
    if (stage.type === 'ramp') {
        const start = new Date(stage.startTime);
        const now = new Date();
        return (now - start) / (1000 * 60 * 60 * 24); // em dias
    }
    
    // Para outras etapas, verificar quando atingiu a temperatura alvo
    if (!state.stageTimers[stageKey]?.targetReachedTime) {
        // Ainda não atingiu o alvo, procurar na história de leituras
        const targetReachedTime = findTargetReachedTime(stageIndex);
        
        if (targetReachedTime) {
            // Salvar quando atingiu o alvo pela primeira vez
            if (!state.stageTimers[stageKey]) {
                state.stageTimers[stageKey] = {};
            }
            state.stageTimers[stageKey].targetReachedTime = targetReachedTime;
        } else {
            return 0; // Nunca atingiu o alvo
        }
    }
    
    const targetReachedTime = state.stageTimers[stageKey].targetReachedTime;
    const now = new Date();
    return (now - targetReachedTime) / (1000 * 60 * 60 * 24); // em dias
}

/**
 * Procura na história quando a temperatura atingiu o alvo pela primeira vez
 */
function findTargetReachedTime(stageIndex) {
    if (!state.config || !state.readings.length) return null;
    
    const stage = state.config.stages[stageIndex];
    if (!stage || !stage.startTime) return null;
    
    const stageStartTime = new Date(stage.startTime);
    
    // Filtrar leituras após o início da etapa
    const relevantReadings = state.readings.filter(r => 
        r.timestamp >= stageStartTime
    );
    
    // Ordenar por timestamp (mais antigo primeiro)
    relevantReadings.sort((a, b) => a.timestamp - b.timestamp);
    
    // Encontrar primeira leitura dentro da tolerância
    const targetTemp = stage.targetTemp || calculateCurrentTargetTemp(stage);
    
    for (const reading of relevantReadings) {
        if (isTemperatureOnTarget(reading.tempFermenter, targetTemp)) {
            return new Date(reading.timestamp);
        }
    }
    
    return null; // Nunca atingiu o alvo
}

function updateStageStatus() {
    if (!state.config || !state.latestReading) return;
    
    const currentStageIndex = state.config.currentStageIndex;
    const currentStage = state.config.stages[currentStageIndex];
    
    if (!currentStage) return;
    
    const currentTemp = state.latestReading.tempFermenter;
    let targetTemp = currentStage.targetTemp;
    
    // Para rampas, calcular temperatura alvo atual
    if (currentStage.type === 'ramp') {
        targetTemp = calculateCurrentTargetTemp(currentStage) || targetTemp;
    }
    
    const stageKey = `${state.config.id}_${currentStageIndex}`;
    
    // Se JÁ ATINGIU O ALVO UMA VEZ, NÃO VERIFICA MAIS
    if (state.stageTimers[stageKey]?.targetReachedTime) {
        console.log(`⏱️ Etapa ${currentStageIndex + 1} já atingiu alvo. Contagem contínua.`);
        return; // NÃO VERIFICA MAIS - mantém contagem
    }
    
    // VERIFICAÇÃO APENAS SE NUNCA ATINGIU O ALVO
    // Verificar se atingiu a temperatura alvo pela PRIMEIRA VEZ
    if (isTemperatureOnTarget(currentTemp, targetTemp)) {
        if (!state.stageTimers[stageKey]) {
            state.stageTimers[stageKey] = {};
        }
        
        // Registrar quando atingiu o alvo pela PRIMEIRA VEZ
        state.stageTimers[stageKey].targetReachedTime = new Date();
        
        console.log(`🎯 PRIMEIRA VEZ! Etapa ${currentStageIndex + 1} atingiu temperatura alvo!`);
        console.log(`⏱️ Iniciando contagem PERMANENTE às ${new Date().toLocaleTimeString()}`);
        
        saveStageTimers();
        
        renderUI();
    }
}

// ========== LISTENER DE AUTENTICAÇÃO ==========
function setupAuthListener() {
    try {
        // Importar dinamicamente
        import("https://www.gstatic.com/firebasejs/9.22.0/firebase-auth.js")
            .then(({ onAuthStateChanged }) => {
                console.log('Configurando listener de autenticação...');
                
                onAuthStateChanged(auth, (user) => {
                    console.log('Estado de autenticação mudou:', user ? `Autenticado: ${user.email}` : 'Não autenticado');
                    
                    if (user) {
                        hideLoginScreen();
                        if (!isAppInitialized) {
                            initAppAfterAuth();
                            isAppInitialized = true;
                        } else {
                            // Se já inicializado, apenas recarregar dados
                            loadActiveConfig();
                        }
                    } else {
                        showLoginScreen();
                        isAppInitialized = false;
                        
                        // Limpar estado
                        state.config = null;
                        state.readings = [];
                        state.latestReading = null;
                        state.rampProgress = null;
                        state.currentTargetTemp = null;
                        state.stageTimers = {};
                        
                        // Parar intervalos
                        if (refreshInterval) {
                            clearInterval(refreshInterval);
                            refreshInterval = null;
                        }
                        
                        // Destruir gráfico
                        if (chart) {
                            chart.destroy();
                            chart = null;
                        }
                    }
                });
            })
            .catch(error => {
                console.error('Erro ao configurar listener de autenticação:', error);
            });
    } catch (error) {
        console.error('Erro no setupAuthListener:', error);
    }
}

// ========== FUNÇÕES DE RAMPA ==========
function calculateRampProgress(stage) {
    if (!stage || stage.type !== 'ramp' || !stage.startTime) {
        return null;
    }
    
    const startTime = new Date(stage.startTime);
    const now = new Date();
    const elapsedMs = now - startTime;
    const elapsedHours = elapsedMs / (1000 * 60 * 60); // Converter para horas
    
    if (elapsedHours <= 0) return 0;
    if (elapsedHours >= stage.rampTime) return 1;
    
    return elapsedHours / stage.rampTime;
}

function calculateCurrentTargetTemp(stage) {
    if (!stage) return null;
    
    // Se for uma rampa, calcular temperatura atual baseada no progresso
    if (stage.type === 'ramp' && stage.startTime) {
        const progress = calculateRampProgress(stage);
        if (progress !== null) {
            const tempDiff = stage.targetTemp - stage.startTemp;
            const currentTemp = stage.startTemp + (tempDiff * progress);
            return currentTemp;
        }
    }
    
    // Para outras etapas, retornar temperatura alvo fixa
    return stage.targetTemp;
}

function updateControlSetpoint() {
    if (!state.config || !state.config.stages) return;
    
    const currentStage = state.config.stages[state.config.currentStageIndex];
    if (!currentStage) return;
    
    // Calcular temperatura alvo atual (considerando rampas)
    state.currentTargetTemp = calculateCurrentTargetTemp(currentStage);
    
    // Atualizar o Firebase com o setpoint atual
    updateSetpointInFirebase();
}

async function updateSetpointInFirebase() {
    if (!state.currentTargetTemp) return;
    
    try {
        const db = requireFirebaseDatabase();
        const setpointRef = await db.ref('setpoint');
        await db.update(setpointRef, {
            temperature: state.currentTargetTemp,
            timestamp: new Date().toISOString(),
            configId: state.config?.id,
            stageIndex: state.config?.currentStageIndex
        });
        console.log('Setpoint atualizado:', state.currentTargetTemp.toFixed(1), '°C');
    } catch (error) {
        console.error('Erro ao atualizar setpoint:', error);
    }
}

// ========== FUNÇÕES DO FIREBASE ==========

async function setupRealtimeListener() {
    try {
        console.log('Configurando listener em tempo real...');
        
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        
        console.log('Listener configurado em:', activeRef.toString());
        
        db.onValue(activeRef, async (snapshot) => {
            const activeData = snapshot.val();
            console.log('Mudança em active:', activeData);
            
            if (activeData && activeData.active === true && activeData.id) {
                await loadConfigById(activeData.id);
                await loadReadings();
            } else {
                state.config = null;
                renderNoActiveFermentation();
            }
        }, (error) => {
            console.error('Erro no listener:', error);
        });
        
    } catch (error) {
        console.error('Erro ao configurar listener em tempo real:', error);
    }
}

async function loadConfigById(configId) {
    try {
        const db = requireFirebaseDatabase();
        const configRef = await db.ref(`configurations/${configId}`);
        const snapshot = await db.get(configRef);
        
        if (snapshot.exists()) {
            const configData = snapshot.val();
            
            if (state.config && state.config.id !== configId) {
                clearStageTimersForConfig(state.config.id);
            }
            
            state.config = { id: configId, ...configData };
            console.log('Configuração carregada:', state.config.name);
            
            // Calcular temperatura alvo atual
            updateControlSetpoint();
            renderUI();
        } else {
            console.log('Configuração não encontrada:', configId);
            state.config = null;
            renderNoActiveFermentation();
        }
    } catch (error) {
        console.error('Erro ao carregar configuração:', error);
    }
}

async function loadActiveConfig() {
    try {
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        const snapshot = await db.get(activeRef);
        
        if (!snapshot.exists()) {
            // Se o nó active não existe, criá-lo
            await initializeActiveNode();
            state.config = null;
            renderNoActiveFermentation();
            return;
        }
        
        const activeData = snapshot.val();
        
        if (activeData.active === true && activeData.id) {
            await loadConfigById(activeData.id);
            await loadReadings();
        } else {
            state.config = null;
            renderNoActiveFermentation();
        }
    } catch (error) {
        console.error('Erro ao carregar configuração ativa:', error);
    }
}

// Função para inicializar o nó active
async function initializeActiveNode() {
    try {
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        await db.set(activeRef, {
            active: false,
            id: null,
            lastUpdated: new Date().toISOString()
        });
        console.log('✅ Nó "active" inicializado no Firebase');
    } catch (error) {
        console.error('Erro ao inicializar nó active:', error);
    }
}

async function loadReadings() {
    if (!state.config) return;
    
    try {
        const db = requireFirebaseDatabase();

        // CHAMA A LIMPEZA
        await performBackgroundCleanup(state.config.id);

        const readingsRef = await db.ref(`readings/${state.config.id}`);
        const snapshot = await db.get(readingsRef);
        
        if (snapshot.exists()) {
            const data = snapshot.val();
            const readingsArray = Object.entries(data)
                .map(([id, reading]) => ({
                    id,
                    ...reading,
                    timestamp: new Date(reading.timestamp)
                }))
                .sort((a, b) => a.timestamp - b.timestamp);
            
            state.readings = readingsArray;
            if (readingsArray.length > 0) {
                const newLatest = readingsArray[readingsArray.length - 1];
                
                // Só enviar se for uma leitura NOVA (não a mesma de antes)
                if (!state.latestReading || newLatest.id !== state.latestReading.id) {
                    state.latestReading = newLatest;
                    
                    // Verificar status da etapa
                    updateStageStatus();
                    
                }
                renderUI();
            }
        } else {
            state.readings = [];
            state.latestReading = null;
            renderUI();
        }
    } catch (error) {
        console.error('Erro ao carregar e limpar leituras:', error);
    }
}

// ========== FUNÇÃO AUXILIAR DE LIMPEZA EM SEGUNDO PLANO ==========
async function performBackgroundCleanup(configId) {
    if (!configId) return;
    
    try {
        const db = requireFirebaseDatabase();
        
        // Executa otimização (leve) a cada carregamento
        await db.optimizeReadings(configId);
        
        // Executa limpeza pesada apenas se passaram 6 horas
        await db.cleanOldReadings(configId);
        
    } catch (error) {
        console.error('Erro na limpeza em background:', error);
        // Não interrompe o fluxo principal por erro de limpeza
    }
}

// ========== FUNÇÃO GETTIMEREMAINING ATUALIZADA ==========
function getTimeRemaining() {
    const stage = getCurrentStage();
    if (!stage || !stage.startTime) return null;

    const stageKey = `${state.config.id}_${state.config.currentStageIndex}`;
    
    // Para rampas, usar tempo total desde início
    if (stage.type === 'ramp') {
        const start = new Date(stage.startTime);
        const now = new Date();
        const elapsedHours = (now - start) / (1000 * 60 * 60);
        const remaining = stage.rampTime - elapsedHours;
        
        if (remaining < 24) {
            return {
                value: remaining > 0 ? remaining.toFixed(1) : 0,
                unit: 'horas',
                status: 'running'
            };
        } else {
            return {
                value: (remaining / 24) > 0 ? (remaining / 24).toFixed(1) : 0,
                unit: 'dias',
                status: 'running'
            };
        }
    }
    
    // Para outras etapas, verificar se já atingiu o alvo
    const targetReachedTime = state.stageTimers[stageKey]?.targetReachedTime;
    
    if (!targetReachedTime) {
        // Ainda não atingiu o alvo
        if (stage.type === 'temperature') {
            return {
                value: stage.duration.toFixed(1),
                unit: 'dias',
                status: 'waiting'
            };
        } else if (stage.type === 'gravity_time') {
            return {
                value: stage.maxDuration.toFixed(1),
                unit: 'dias',
                status: 'waiting'
            };
        }
        return null;
    }
    
    // Já atingiu o alvo, calcular tempo restante
    const start = targetReachedTime;
    const now = new Date();
    
    if (stage.type === 'temperature') {
        const elapsed = (now - start) / (1000 * 60 * 60 * 24);
        const remaining = stage.duration - elapsed;
        return {
            value: remaining > 0 ? remaining.toFixed(1) : 0,
            unit: 'dias',
            status: 'running'
        };
    } else if (stage.type === 'gravity_time') {
        const elapsed = (now - start) / (1000 * 60 * 60 * 24);
        const remaining = stage.maxDuration - elapsed;
        return {
            value: remaining > 0 ? remaining.toFixed(1) : 0,
            unit: 'dias',
            status: 'running'
        };
    }

    return null;
}

// ========== FUNÇÕES AUXILIARES ==========
function getCurrentStage() {
    if (!state.config || !state.config.stages) return null;
    return state.config.stages[state.config.currentStageIndex];
}

function getStageDescription(stage) {
    if (!stage) return '';
    
    const stageKey = `${state.config?.id}_${state.config?.stages?.indexOf(stage)}`;
    const targetReached = state.stageTimers[stageKey]?.targetReachedTime;
    
    switch(stage.type) {
        case 'temperature':
            const tempStatus = targetReached ? '✅ No alvo' : '⏳ Aguardando alvo';
            return `${stage.targetTemp}°C por ${stage.duration} dias (${tempStatus})`;
            
        case 'gravity':
            const gravityStatus = targetReached ? '✅ No alvo' : '⏳ Aguardando alvo';
            return `${stage.targetTemp}°C até ${stage.targetGravity} SG (${gravityStatus})`;
            
        case 'gravity_time':
            const gravityTimeStatus = targetReached ? '✅ No alvo' : '⏳ Aguardando alvo';
            return `${stage.targetTemp}°C até ${stage.targetGravity} SG (máx ${stage.maxDuration} dias) (${gravityTimeStatus})`;
            
        case 'ramp':
            const progress = calculateRampProgress(stage);
            const currentTemp = calculateCurrentTargetTemp(stage);
            const direction = stage.direction === 'up' ? '▲' : '▼';
            const rampTimeDisplay = stage.rampTime < 24 
                ? `${stage.rampTime} horas` 
                : `${(stage.rampTime / 24).toFixed(1)} dias`;
            const rampStatus = '⏱️ Em progresso';
            return `${direction} ${stage.startTemp}°C → ${stage.targetTemp}°C em ${rampTimeDisplay} (Atual: ${currentTemp ? currentTemp.toFixed(1) : '?'}°C, ${progress ? (progress * 100).toFixed(0) : '0'}%) (${rampStatus})`;
        default:
            return '';
    }
}

function saveStageTimers() {
    try {
        // Converter objetos Date para strings ISO para armazenamento
        const timersToSave = JSON.parse(JSON.stringify(state.stageTimers));
        localStorage.setItem('stageTimers', JSON.stringify(timersToSave));
        console.log('💾 Temporizadores salvos:', timersToSave);
    } catch (error) {
        console.error('Erro ao salvar temporizadores:', error);
    }
}

function clearStageTimersForConfig(configId) {
    Object.keys(state.stageTimers).forEach(key => {
        if (key.startsWith(configId + '_')) {
            delete state.stageTimers[key];
        }
    });
    saveStageTimers();
    console.log(`🧹 Temporizadores limpos para configuração ${configId}`);
}

// ========== TEMPLATES HTML ==========
const cardTemplate = ({ title, icon, value, subtitle, color, clickable = false }) => `
    <div class="card ${clickable ? 'cursor-pointer hover:shadow-lg transition-shadow' : ''}" >
        <div class="flex items-center gap-3 mb-2">
            <i class="${icon}" style="color: ${color}; font-size: 1.5rem;"></i>
            <span class="text-sm font-medium text-gray-600">${title}</span>
        </div>
        <div class="text-3xl font-bold text-gray-800">
            ${value}
        </div>
        ${subtitle ? `<div class="text-sm text-gray-600 mt-1">${subtitle}</div>` : ''}
    </div>
`;

const stageTemplate = (stage, index, currentIndex, targetReached, isCurrent) => {
    const isRamp = stage.type === 'ramp';
    const progress = isRamp && isCurrent ? calculateRampProgress(stage) : null;
    
    // Determinar cor baseado no status
    let borderColor = 'border-gray-200';
    let bgColor = 'bg-gray-50';
    let statusText = 'Aguardando';
    
    if (isCurrent) {
        if (targetReached || isRamp) {
            borderColor = 'border-blue-500';
            bgColor = 'bg-blue-50';
            statusText = 'Em andamento';
        } else {
            borderColor = 'border-yellow-500';
            bgColor = 'bg-yellow-50';
            statusText = 'Aguardando alvo';
        }
    } else if (stage.status === 'completed') {
        borderColor = 'border-green-500';
        bgColor = 'bg-green-50';
        statusText = 'Concluída';
    }
    
    return `
    <div class="p-4 rounded-lg border-2 ${borderColor} ${bgColor}">
        <div class="flex justify-between items-start">
            <div>
                <h3 class="font-semibold text-gray-800">
                    ${isRamp ? '<i class="fas fa-chart-line text-blue-600 mr-2"></i>' : ''}
                    Etapa ${index + 1}
                    ${isCurrent ? `<span class="ml-2 text-sm ${targetReached ? 'text-blue-600' : 'text-yellow-600'}">(${statusText})</span>` : ''}
                    ${stage.status === 'completed' ? '<span class="ml-2 text-sm text-green-600">(Concluída)</span>' : ''}
                </h3>
                <p class="text-sm text-gray-600 mt-1">
                    ${getStageDescription(stage)}
                </p>
                ${isRamp && isCurrent && progress !== null ? `
                    <div class="mt-2">
                        <div class="w-full bg-gray-200 rounded-full h-2">
                            <div class="bg-blue-600 h-2 rounded-full" 
                                 style="width: ${progress * 100}%"></div>
                        </div>
                        <div class="text-xs text-gray-500 mt-1">
                            Progresso: ${(progress * 100).toFixed(0)}%
                        </div>
                    </div>
                ` : ''}
                ${targetReached && !isRamp ? `
                    <div class="mt-2 text-xs text-green-600">
                        <i class="fas fa-check-circle"></i> Alvo atingido em ${new Date(targetReached).toLocaleDateString('pt-BR')}
                    </div>
                ` : ''}
            </div>
            <span class="badge ${
                isCurrent && targetReached ? 'badge-active' :
                isCurrent && !targetReached ? 'badge-waiting' :
                stage.status === 'completed' ? 'badge-completed' :
                'badge-pending'
            }">
                ${isCurrent && targetReached ? 'Ativa' :
                 isCurrent && !targetReached ? 'Aguardando alvo' :
                 stage.status === 'running' ? 'Ativa' :
                 stage.status === 'completed' ? 'Concluída' : 'Aguardando'}
            </span>
        </div>
    </div>
`};

// ========== FUNÇÕES DE RENDERIZAÇÃO ==========
function renderUI() {
    if (!state.config) {
        renderNoActiveFermentation();
        return;
    }

    // Atualizar cabeçalho
    const nameElement = document.getElementById('fermentation-name');
    const stageElement = document.getElementById('stage-info');
    
    if (nameElement) nameElement.textContent = state.config.name;
    if (stageElement) {
        stageElement.textContent = 
            `Etapa ${state.config.currentStageIndex + 1} de ${state.config.stages.length}`;
    }
    
    const timeRemaining = getTimeRemaining();
    const timeElement = document.getElementById('time-remaining');
    
    if (timeElement) {
        if (timeRemaining !== null) {
            let icon = 'fas fa-clock';
            let statusClass = '';
            
            if (timeRemaining.status === 'waiting') {
                icon = 'fas fa-hourglass-start';
                statusClass = 'text-yellow-600';
            } else if (timeRemaining.status === 'running') {
                icon = 'fas fa-hourglass-half';
                statusClass = 'text-green-600';
            }
            
            timeElement.innerHTML = `
                <i class="${icon} ${statusClass}"></i> 
                <span class="${statusClass}">
                    ${timeRemaining.value} ${timeRemaining.unit} ${timeRemaining.status === 'waiting' ? '(aguardando alvo)' : 'restantes'}
                </span>
            `;
            timeElement.style.display = 'flex';
        } else {
            timeElement.style.display = 'none';
        }
    }

    // Atualizar status da temperatura
    updateStageStatus();

    // Renderizar cards de informação
    const currentStage = getCurrentStage();
    const infoCards = document.getElementById('info-cards');
    
    if (infoCards) {
        const stageKey = `${state.config.id}_${state.config.currentStageIndex}`;
        const targetReached = state.stageTimers[stageKey]?.targetReachedTime;
        const currentTemp = state.latestReading?.tempFermenter;
        const targetTemp = currentStage ? 
            (currentStage.type === 'ramp' ? 
                calculateCurrentTargetTemp(currentStage) : 
                currentStage.targetTemp) : 
            null;
        
        let tempStatus = '';
        let tempColor = '#9ca3af';
        
        if (currentTemp && targetTemp) {
            if (isTemperatureOnTarget(currentTemp, targetTemp)) {
                tempStatus = '✅ No alvo';
                tempColor = '#10b981';
            } else if (currentTemp < targetTemp) {
                tempStatus = '⬆️ Abaixo do alvo';
                tempColor = '#3b82f6';
            } else {
                tempStatus = '⬇️ Acima do alvo';
                tempColor = '#ef4444';
            }
        }

        infoCards.innerHTML = `
            ${cardTemplate({
                title: 'Temp. Geladeira',
                icon: 'fas fa-thermometer-half',
                value: state.latestReading ? `${state.latestReading.tempFridge.toFixed(1)}°C` : '--',
                color: '#3b82f6'
            })}
            ${cardTemplate({
                title: 'Temp. Fermentador',
                icon: 'fas fa-thermometer-full',
                value: state.latestReading ? `${state.latestReading.tempFermenter.toFixed(1)}°C` : '--',
                subtitle: tempStatus,
                color: tempColor
            })}
            ${cardTemplate({
                title: 'Gravidade Atual',
                icon: 'fas fa-tint',
                value: state.latestReading && state.latestReading.gravity ? 
                       state.latestReading.gravity.toFixed(3) : '--',
                color: '#10b981'
            })}
            ${cardTemplate({
                title: 'Temp. Alvo Atual',
                icon: 'fas fa-bullseye',
                value: state.currentTargetTemp ? `${state.currentTargetTemp.toFixed(1)}°C` : '--',
                subtitle: targetReached ? '✅ Contagem iniciada' : '⏳ Aguardando alvo',
                color: targetReached ? '#10b981' : '#f59e0b'
            })}
        `;
        
    }

    // Renderizar gráfico
    renderChart();

    // Renderizar lista de etapas
    const stagesList = document.getElementById('stages-list');
    if (stagesList) {
        stagesList.innerHTML = state.config.stages
            .map((stage, index) => {
                const stageKey = `${state.config.id}_${index}`;
                const targetReached = state.stageTimers[stageKey]?.targetReachedTime;
                const isCurrent = index === state.config.currentStageIndex;
                
                return stageTemplate(stage, index, state.config.currentStageIndex, targetReached, isCurrent);
            })
            .join('');
    }
}

function renderChart() {
    const canvas = document.getElementById('fermentation-chart');
    const ctx = canvas ? canvas.getContext('2d') : null;
    const noDataMsg = document.getElementById('no-data-message');

    if (!ctx || state.readings.length === 0) {
        if (canvas) canvas.style.display = 'none';
        if (noDataMsg) noDataMsg.style.display = 'block';
        return;
    }

    if (canvas) canvas.style.display = 'block';
    if (noDataMsg) noDataMsg.style.display = 'none';

    // Destruir gráfico anterior
    if (chart) {
        chart.destroy();
    }

    // Preparar dados
    const labels = state.readings.map(r => 
        r.timestamp.toLocaleTimeString('pt-BR', { 
            hour: '2-digit', 
            minute: '2-digit',
            day: '2-digit',
            month: '2-digit'
        })
    );

    const currentStage = getCurrentStage();
    
    // Calcular linha teórica da rampa
    let rampData = null;
    if (currentStage && currentStage.type === 'ramp' && currentStage.startTime) {
        const startTime = new Date(currentStage.startTime);
        rampData = state.readings.map(r => {
            const readingTime = r.timestamp;
            const elapsedMs = readingTime - startTime;
            const elapsedHours = elapsedMs / (1000 * 60 * 60);
            
            if (elapsedHours <= 0) return currentStage.startTemp;
            if (elapsedHours >= currentStage.rampTime) return currentStage.targetTemp;
            
            const progress = elapsedHours / currentStage.rampTime;
            return currentStage.startTemp + (currentStage.targetTemp - currentStage.startTemp) * progress;
        });
    }

    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Temp. Geladeira',
                    data: state.readings.map(r => r.tempFridge),
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Fermentador',
                    data: state.readings.map(r => r.tempFermenter),
                    borderColor: '#1e40af',
                    backgroundColor: 'rgba(30, 64, 175, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Alvo Teórica',
                    data: rampData || state.readings.map(r => r.tempTarget),
                    borderColor: '#ef4444',
                    borderDash: [5, 5],
                    backgroundColor: 'rgba(239, 68, 68, 0.1)',
                    tension: 0,
                    fill: false,
                    pointRadius: 0
                },
                {
                    label: 'Gravidade (x1000)',
                    data: state.readings.map(r => r.gravity ? r.gravity * 1000 : null),
                    borderColor: '#10b981',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    tension: 0.4,
                    fill: false,
                    yAxisID: 'y1'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Temperatura (°C)'
                    },
                    min: function(context) {
                        const currentStage = getCurrentStage();
                        if (currentStage && currentStage.type === 'ramp') {
                            return Math.min(currentStage.startTemp, currentStage.targetTemp) - 2;
                        }
                        return 0;
                    },
                    max: function(context) {
                        const currentStage = getCurrentStage();
                        if (currentStage && currentStage.type === 'ramp') {
                            return Math.max(currentStage.startTemp, currentStage.targetTemp) + 2;
                        }
                        return 30;
                    }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Gravidade (x1000)'
                    },
                    grid: {
                        drawOnChartArea: false
                    }
                }
            },
            plugins: {
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            if (label) {
                                label += ': ';
                            }
                            if (context.datasetIndex === 3) { // Gravidade
                                const gravityValue = context.raw / 1000;
                                label += gravityValue.toFixed(3) + ' SG';
                            } else {
                                label += context.parsed.y.toFixed(1) + '°C';
                            }
                            return label;
                        }
                    }
                }
            }
        }
    });

    // Adicionar linha de referência para gravidade alvo
    if (currentStage && (currentStage.type === 'gravity' || currentStage.type === 'gravity_time')) {
        const targetGravityLine = currentStage.targetGravity * 1000;
        
        chart.data.datasets.push({
            label: 'Meta Gravidade',
            data: new Array(labels.length).fill(targetGravityLine),
            borderColor: '#059669',
            borderDash: [3, 3],
            pointRadius: 0,
            yAxisID: 'y1'
        });
        
        chart.update();
    }
}

function renderNoActiveFermentation() {
    const container = document.querySelector('.container');
    if (container) {
        container.innerHTML = `
            <div class="min-h-screen flex items-center justify-center">
                <div class="card max-w-md text-center">
                    <i class="fas fa-chart-line" style="font-size: 4rem; color: #9ca3af; margin-bottom: 1rem;"></i>
                    <h2 class="text-2xl mb-2">Nenhuma Fermentação Ativa</h2>
                    <p class="text-gray-600 mb-4">
                        Configure e inicie uma fermentação para monitorá-la aqui.
                    </p>
                    <button onclick="location.href='config.html'" class="btn btn-primary">
                        <i class="fas fa-cog"></i> Ir para Configuração
                    </button>
                </div>
            </div>
        `;
    }
}

// ========== INICIALIZAÇÃO DO APP APÓS AUTENTICAÇÃO ==========
async function initAppAfterAuth() {
    console.log('Inicializando app após autenticação');
    
    try {
        // Resetar tempos
        lastCleanupTime = 0;
        
        // Carregar temporizadores do localStorage
        const savedTimers = localStorage.getItem('stageTimers');
        if (savedTimers) {
            try {
                const parsedTimers = JSON.parse(savedTimers);
                // Converter strings de data de volta para objetos Date
                Object.keys(parsedTimers).forEach(key => {
                    if (parsedTimers[key]?.targetReachedTime) {
                        parsedTimers[key].targetReachedTime = new Date(parsedTimers[key].targetReachedTime);
                    }
                });
                state.stageTimers = parsedTimers;
                console.log('⏱️ Temporizadores restaurados do localStorage:', state.stageTimers);
            } catch (error) {
                console.error('Erro ao restaurar temporizadores:', error);
                state.stageTimers = {};
            }
        } else {
            state.stageTimers = {};
        }
        
        // 1. Verificar e inicializar nó active se necessário
        const activeStatus = await checkActiveStatus();
        console.log('Status da fermentação ativa:', activeStatus);
        
        if (activeStatus.active && activeStatus.id) {
            await loadConfigById(activeStatus.id);
            await loadReadings();
        } else {
            renderNoActiveFermentation();
        }
        
        // 2. Configurar listener em tempo real
        await setupRealtimeListener();
        
        // 3. Configurar polling para atualizar temperatura alvo das rampas
        if (refreshInterval) {
            clearInterval(refreshInterval);
        }
        
        refreshInterval = setInterval(() => {
            if (state.config) {
                // Atualizar cálculo das rampas
                updateControlSetpoint();
                
                // Verificar status das etapas
                updateStageStatus();
                
                // Atualizar leituras
                loadReadings();
            }
        }, CHECK_INTERVAL); // Usar intervalo de verificação
        
        console.log('App inicializado com sucesso após autenticação');
        
    } catch (error) {
        console.error('Erro na inicialização após autenticação:', error);
        alert('Erro ao inicializar o monitor. Por favor, recarregue a página.');
    }
}

/**
 * Ativa uma fermentação específica
 */
async function activateFermentation(configId) {
    try {
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        
        await db.update(activeRef, {
            active: true,
            id: configId,
            activatedAt: new Date().toISOString(),
            lastUpdated: new Date().toISOString()
        });
        
        console.log(`✅ Fermentação ${configId} ativada`);
        return true;
    } catch (error) {
        console.error('Erro ao ativar fermentação:', error);
        return false;
    }
}

/**
 * Desativa a fermentação atual
 */
async function deactivateFermentation() {
    try {
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        
        await db.update(activeRef, {
            active: false,
            id: null,
            deactivatedAt: new Date().toISOString(),
            lastUpdated: new Date().toISOString()
        });
        
        // Limpar estado local
        state.config = null;
        state.readings = [];
        state.latestReading = null;
        state.rampProgress = null;
        state.currentTargetTemp = null;
        state.stageTimers = {};
        
        console.log('✅ Fermentação desativada');
        renderNoActiveFermentation();
        return true;
    } catch (error) {
        console.error('Erro ao desativar fermentação:', error);
        return false;
    }
}

/**
 * Verifica o status atual da fermentação ativa
 */
async function checkActiveStatus() {
    try {
        const db = requireFirebaseDatabase();
        const activeRef = await db.ref('active');
        const snapshot = await db.get(activeRef);
        
        if (!snapshot.exists()) {
            await initializeActiveNode();
            return { active: false, id: null };
        }
        
        return snapshot.val();
    } catch (error) {
        console.error('Erro ao verificar status ativo:', error);
        return { active: false, id: null };
    }
}

// ========== EXPORTAÇÃO PARA ESCOPO GLOBAL ==========
window.login = login;
window.logout = logout;
window.refreshData = loadActiveConfig;
window.activateFermentation = activateFermentation;
window.deactivateFermentation = deactivateFermentation;
window.checkActiveStatus = checkActiveStatus;

// ========== INICIALIZAÇÃO ==========
document.addEventListener('DOMContentLoaded', async () => {
    console.log('App.js carregado. Inicializando Firebase...');
    
    // Adicionar evento de Enter para login
    const emailInput = document.getElementById('login-email');
    const passwordInput = document.getElementById('login-password');
    
    if (emailInput && passwordInput) {
        const handleEnterKey = (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                login();
            }
        };
        
        emailInput.addEventListener('keypress', handleEnterKey);
        passwordInput.addEventListener('keypress', handleEnterKey);
    }
    
    try {
        const initialized = await initializeFirebase();
        if (initialized) {
            setupAuthListener();
            console.log('Aplicação inicializada com sucesso');
        }
    } catch (error) {
        console.error('Erro ao inicializar aplicação:', error);
        alert('Erro ao inicializar o sistema. Por favor, recarregue a página.');
    }
});