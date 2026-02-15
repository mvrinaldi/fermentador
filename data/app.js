// app.js - Monitor Passivo (COM visualização de Cooler/Heater)
const API_BASE_URL = '/api.php?path=';

// ========== CONFIGURAÇÃO DE DEBUG ==========
const DEBUG_MODE = true; // false para produção, true para debug

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;

const REFRESH_INTERVAL = 30000;
const ESP_OFFLINE_THRESHOLD = 120000;
const ISPINDEL_STALE_THRESHOLD = 3600000; // 1 hora em ms
const SAO_PAULO_UTC_OFFSET = -3 * 60 * 60 * 1000;

// Limites de memória heap do ESP8266 (em bytes)
const HEAP_WARNING_THRESHOLD = 30000;  // Abaixo disso: atenção (amarelo)
const HEAP_CRITICAL_THRESHOLD = 15000; // Abaixo disso: crítico (vermelho)

// ========== ESTADO DA APLICAÇÃO ==========
let appState = {
    config: null,
    espState: null,
    readings: [],
    ispindel: null,
    ispindelReadings: [],
    controller: null,
    controllerHistory: [],
    heartbeat: null,
    lastUpdate: null,
    latestReading: null
};

// ========== FUNÇÕES DE API ==========
async function apiRequest(endpoint, options = {}) {
    try {
        const response = await fetch(API_BASE_URL + endpoint, {
            ...options,
            headers: {
                'Content-Type': 'application/json',
                ...options.headers
            },
            credentials: 'same-origin'
        });
        
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Erro na requisição');
        }
        
        return await response.json();
    } catch (error) {
        throw error;
    }
}

// ========== AUTENTICAÇÃO ==========
async function login() {
    const email = document.getElementById('login-email').value;
    const password = document.getElementById('login-password').value;
    
    if (!email || !password) {
        showError('Por favor, preencha email e senha');
        return;
    }
    
    try {
        await apiRequest('auth/login', {
            method: 'POST',
            body: JSON.stringify({ email, password })
        });
        
        hideError();
        hideLoginScreen();
        await initAppAfterAuth();
        
    } catch (error) {
        showError(error.message || 'Erro no login');
    }
}

async function logout() {
    try {
        await apiRequest('auth/logout', { method: 'POST' });
        
        appState = {
            config: null,
            espState: null,
            readings: [],
            ispindel: null,
            ispindelReadings: [],
            controller: null,
            controllerHistory: [],
            heartbeat: null,
            lastUpdate: null,
            latestReading: null
        };
        
        if (refreshInterval) {
            clearInterval(refreshInterval);
            refreshInterval = null;
        }
        
        if (chart) {
            chart.destroy();
            chart = null;
        }
        
        showLoginScreen();
        
    } catch (error) {
        alert('Erro ao fazer logout: ' + error.message);
    }
}

async function checkAuthStatus() {
    try {
        const result = await apiRequest('auth/check');
        
        if (result.authenticated) {
            hideLoginScreen();
            if (!isAppInitialized) {
                await initAppAfterAuth();
                isAppInitialized = true;
            }
        } else {
            showLoginScreen();
        }
    } catch (error) {
        showLoginScreen();
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
    
    if (loginScreen) loginScreen.style.display = 'flex';
    if (container) container.style.display = 'none';
}

function hideLoginScreen() {
    const loginScreen = document.getElementById('login-screen');
    const container = document.querySelector('.container');
    
    if (loginScreen) loginScreen.style.display = 'none';
    if (container) container.style.display = 'block';
}

// ========== DATA/HORA ==========
function utcToSaoPaulo(dateString) {
    if (!dateString) {
        const now = new Date();
        return new Date(now.getTime() + SAO_PAULO_UTC_OFFSET);
    }
    
    let date;
    
    if (dateString instanceof Date) {
        date = dateString;
    } else {
        let normalized = dateString;
        if (!dateString.endsWith('Z') && dateString.includes('T')) {
            normalized = dateString + 'Z';
        }
        date = new Date(normalized);
        
        if (isNaN(date.getTime())) {
            return new Date(new Date().getTime() + SAO_PAULO_UTC_OFFSET);
        }
    }
    
    return new Date(date.getTime() + SAO_PAULO_UTC_OFFSET);
}

function calculateTimeDifference(utcTimestamp) {
    if (!utcTimestamp) return Infinity;
    
    const nowUTC = new Date();
    const timestampUTC = new Date(utcTimestamp);
    
    return nowUTC - timestampUTC;
}

function formatTimeDifference(ms) {
    if (ms === Infinity) return 'sem dados';
    
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}d ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}min`;
    if (minutes > 0) return `${minutes} minuto${minutes > 1 ? 's' : ''}`;
    return `${seconds} segundo${seconds > 1 ? 's' : ''}`;
}

function formatTimestamp(date) {
    if (!date) return '--:--';
    
    const saoPauloDate = utcToSaoPaulo(date);
    
    const day = saoPauloDate.getDate().toString().padStart(2, '0');
    const month = (saoPauloDate.getMonth() + 1).toString().padStart(2, '0');
    const year = saoPauloDate.getFullYear();
    const hour = saoPauloDate.getHours().toString().padStart(2, '0');
    const minute = saoPauloDate.getMinutes().toString().padStart(2, '0');
    
    return `${day}/${month}/${year} ${hour}:${minute}`;
}

function formatTimeRemaining(tr) {
    if (!tr) {
        return '--';
    }
    
    if (tr.status === 'completed' || tr.unit === 'completed' || tr.display === 'Fermentação concluída') {
        return 'Fermentação concluída';
    }
    
    if (tr.unit === 'detailed' && tr.days !== undefined) {
        const parts = [];
        
        if (tr.days > 0) {
            parts.push(`${tr.days}d`);
        }
        if (tr.hours > 0) {
            parts.push(`${tr.hours}h`);
        }
        if (tr.minutes > 0) {
            parts.push(`${tr.minutes}m`);
        }
        
        if (parts.length === 0) {
            return '< 1m';
        }
        
        return parts.join(' ');
    }
    
    if (tr.value !== undefined && tr.unit) {
        if (tr.unit === 'indefinite' || tr.unit === 'ind') {
            return 'Aguardando gravidade';
        }
        
        if (tr.unit === 'hours') {
            const totalHours = parseFloat(tr.value);
            const hours = Math.floor(totalHours);
            const minutes = Math.round((totalHours - hours) * 60);
            
            if (totalHours < 1) {
                return `${Math.round(totalHours * 60)}min`;
            } else if (minutes > 0) {
                return `${hours}h ${minutes}min`;
            } else {
                return `${hours}h`;
            }
        } 
        else if (tr.unit === 'days') {
            const totalDays = parseFloat(tr.value);
            
            if (totalDays >= 1) {
                return `${totalDays.toFixed(1)} dias`;
            } else {
                const hours = Math.round(totalDays * 24);
                return `${hours}h`;
            }
        }
        else if (tr.unit === 'minutes') {
            return `${Math.round(tr.value)}min`;
        }
        
        return `${tr.value} ${tr.unit}`;
    }
    
    return '--';
}

// ========== FUNÇÃO DE DESCOMPRESSÃO DOS DADOS ==========
function decompressData(data) {
    if (!data || typeof data !== 'object') {
        return data;
    }
    
    const messageMap = {
        'fc': 'Fermentação concluída automaticamente - mantendo temperatura',
        'fconc': 'Fermentação concluída automaticamente - mantendo temperatura',
        'tc': 'Fermentação concluída',
        'ch': 'completed_holding_temp',
        'chold': 'completed_holding_temp',
        'fpaus': 'Fermentação pausada',
        'targ': 'Temperatura alvo atingida',
        'strt': 'Etapa iniciada',
        'ramp': 'Em rampa',
        'w': 'Aguardando alvo',
        'wait': 'Aguardando alvo',
        'r': 'Executando',
        'run': 'Executando',
        'c': 'Resfriando',
        'cool': 'Resfriando',
        'h': 'Aquecendo',
        'heat': 'Aquecendo',
        'i': 'Ocioso',
        'idle': 'Ocioso',
        'peak': 'Detectando pico',
        'err': 'Erro',
        'off': 'Desligado',
        'wg': 'waiting_gravity'
    };
    
    const unitMap = {
        'h': 'hours',
        'd': 'days',
        'm': 'minutes',
        'ind': 'indefinite',
        'tc': 'completed'
    };
    
    const statusMap = {
        'r': 'running',
        'run': 'running',
        'w': 'waiting',
        'wait': 'waiting',
        'wg': 'waiting_gravity',
        'tc': 'completed'
    };
    
    const stageTypeMap = {
        't': 'temperature',
        'r': 'ramp',
        'g': 'gravity',
        'gt': 'gravity_time'
    };
    
    const result = { ...data };
    
    if (DEBUG_MODE) {
        console.log('🔍 DEBUG decompressData INPUT:', result);
    }
    
    const fieldMap = {
        'cn': 'config_name',
        'csi': 'currentStageIndex',
        'ts': 'totalStages',
        'stt': 'stageTargetTemp',
        'ptt': 'pidTargetTemp',
        'ctt': 'currentTargetTemp',
        'c': 'cooling',
        'h': 'heating',
        's': 'status',
        'msg': 'message',
        'cid': 'config_id',
        'ca': 'completedAt',
        'tms': 'timestamp',
        'um': 'uptime_ms',
        'rp': 'rampProgress',
        'st': 'stageType'
    };
    
    Object.keys(fieldMap).forEach(short => {
        if (result[short] !== undefined) {
            result[fieldMap[short]] = result[short];
            delete result[short];
        }
    });
    
    if (result.tr !== undefined) {
        
        if (Array.isArray(result.tr)) {
            if (result.tr.length === 1 && result.tr[0] === 'tc') {
                result.timeRemaining = {
                    value: 0,
                    unit: 'completed',
                    status: 'completed',
                    display: 'Fermentação concluída'
                };
                result.targetReached = true;
                result.fermentationCompleted = true;
                if (DEBUG_MODE) {
                    console.log('✅ tr é ["tc"] - Fermentação concluída');
                }
            }
            else if (result.tr.length === 4 && 
                typeof result.tr[0] === 'number' && 
                typeof result.tr[1] === 'number' && 
                typeof result.tr[2] === 'number') {
                
                result.timeRemaining = {
                    days: result.tr[0],
                    hours: result.tr[1],
                    minutes: result.tr[2],
                    unit: 'detailed',
                    status: statusMap[result.tr[3]] || 
                           messageMap[result.tr[3]] || 
                           result.tr[3] || 'unknown'
                };
                result.targetReached = true;
                
            } else if (result.tr.length >= 3) {
                result.timeRemaining = {
                    value: result.tr[0],
                    unit: unitMap[result.tr[1]] || result.tr[1],
                    status: statusMap[result.tr[2]] || 
                           messageMap[result.tr[2]] || 
                           result.tr[2] || 'unknown'
                };
                result.targetReached = true;
            }
        } 
        else if (typeof result.tr === 'boolean') {
            result.targetReached = result.tr;
        }
        else if (typeof result.tr === 'string' && result.tr === 'tc') {
            result.timeRemaining = {
                value: 0,
                unit: 'completed',
                status: 'completed',
                display: 'Fermentação concluída'
            };
            result.targetReached = true;
            result.fermentationCompleted = true;
            if (DEBUG_MODE) {
                console.log('✅ tr é "tc" (string) - Fermentação concluída');
            }
        }
        
        delete result.tr;
    }
    
    if (result.targetReached === undefined) {
        if (result.timeRemaining) {
            result.targetReached = true;
            if (DEBUG_MODE) {
                console.log('✅ Inferido targetReached = true (tem timeRemaining)');
            }
        } else if (result.status === 'running' || result.status === 'Executando') {
            result.targetReached = false;
            if (DEBUG_MODE) {
                console.log('✅ Inferido targetReached = false (status running, sem timeRemaining)');
            }
        } else if (result.status === 'waiting' || result.status === 'Aguardando') {
            result.targetReached = false;
            if (DEBUG_MODE) {
                console.log('✅ Inferido targetReached = false (status waiting)');
            }
        }
    }
    
    if (result.status && messageMap[result.status]) {
        result.status = messageMap[result.status];
    }
    
    if (result.message && messageMap[result.message]) {
        result.message = messageMap[result.message];
    }
    
    if (result.stageType && stageTypeMap[result.stageType]) {
        result.stageType = stageTypeMap[result.stageType];
    }
    
    if (result.control_status && typeof result.control_status === 'object') {
        const cs = result.control_status;
        
        if (cs.s && messageMap[cs.s]) {
            cs.state = messageMap[cs.s];
            delete cs.s;
        }
        
        const csMap = {
            'iw': 'is_waiting',
            'wr': 'wait_reason',
            'ws': 'wait_seconds',
            'wd': 'wait_display',
            'pd': 'peak_detection',
            'ep': 'estimated_peak'
        };
        
        Object.keys(csMap).forEach(short => {
            if (cs[short] !== undefined) {
                cs[csMap[short]] = cs[short];
                delete cs[short];
            }
        });
    }
    return result;
}

// ========== FUNÇÃO PARA OBTER DADOS DO ISPINDEL ==========
function getIspindelData() {
    const ispindel = appState.ispindel;
    
    if (!ispindel) {
        return {
            gravity: null,
            temperature: null,
            battery: null,
            isStale: true,
            lastUpdate: null,
            staleMessage: 'Sem dados do iSpindel'
        };
    }
    
    const isStale = ispindel.is_stale === true;
    const secondsSinceUpdate = ispindel.seconds_since_update || 0;
    
    let staleMessage = null;
    if (isStale) {
        const hours = Math.floor(secondsSinceUpdate / 3600);
        const minutes = Math.floor((secondsSinceUpdate % 3600) / 60);
        
        if (hours > 0) {
            staleMessage = `Última leitura há ${hours}h ${minutes}min`;
        } else {
            staleMessage = `Última leitura há ${minutes}min`;
        }
    }
    
    return {
        gravity: parseFloat(ispindel.gravity) || null,
        temperature: parseFloat(ispindel.temperature) || null,
        battery: parseFloat(ispindel.battery) || null,
        isStale: isStale,
        lastUpdate: ispindel.reading_timestamp,
        staleMessage: staleMessage
    };
}

// ========== CARREGAMENTO ==========
async function loadCompleteState() {
    try {
        const activeData = await apiRequest('active');
        
        if (!activeData.active || !activeData.id) {
            // Mesmo sem fermentação ativa, busca últimos dados dos sensores
            appState.config = null;
            appState.espState = {};
            appState.readings = [];
            appState.ispindel = null;
            appState.ispindelReadings = [];
            appState.controller = null;
            appState.controllerHistory = [];
            appState.heartbeat = null;
            appState.latestReading = null;
            
            // Tenta buscar últimos dados disponíveis
            try {
                const latestData = await apiRequest('latest-readings');
                if (latestData) {
                    appState.latestReading = latestData.reading || null;
                    appState.ispindel = latestData.ispindel || null;
                }
            } catch (e) {
                if (DEBUG_MODE) {
                    console.log('Não foi possível buscar últimas leituras:', e);
                }
            }
            
            renderNoActiveFermentation();
            return;
        }
        
        const completeState = await apiRequest(`state/complete&config_id=${activeData.id}`);
        
        if (DEBUG_MODE) {
            console.log('🔍 DADOS BRUTOS DO SERVIDOR (completo):', completeState);
        }
        
        if (completeState.state) {
            if (DEBUG_MODE) {
                console.log('🔍 Estado ANTES da descompressão:', completeState.state);
            }
            
            completeState.state = decompressData(completeState.state);
            
            if (DEBUG_MODE) {
                console.log('🔍 Estado APÓS descompressão:', {
                    targetReached: completeState.state.targetReached,
                    timeRemaining: completeState.state.timeRemaining,
                    status: completeState.state.status,
                    config_name: completeState.state.config_name,
                    fermentationCompleted: completeState.state.fermentationCompleted
                });
            }
        }
        
        appState.config = completeState.config;
        appState.espState = completeState.state || {};
        appState.readings = completeState.readings || [];
        appState.ispindel = completeState.ispindel || null;
        appState.ispindelReadings = completeState.ispindel_readings || [];
        appState.controller = completeState.controller;
        appState.controllerHistory = completeState.controller_history || [];
        appState.lastUpdate = completeState.timestamp;
        appState.heartbeat = completeState.heartbeat;
        appState.latestReading = null;
        
        checkESPStatus();
        renderUI();
        
    } catch (error) {
        console.error('Erro ao carregar estado:', error);
        renderNoActiveFermentation();
    }
}

function getStatusText(tr) {
    if (!tr || !tr.status) return '';
    
    const statusMap = {
        'running': 'restantes',
        'waiting': 'aguardando temperatura',
        'waiting_gravity': 'aguardando gravidade',
        'completed': ''
    };
    
    return statusMap[tr.status] || tr.status;
}

async function autoRefreshData() {
    try {
        await loadCompleteState();
    } catch (error) {
        console.error('❌ Erro no auto-refresh:', error);
    }
}

// ========== STATUS ESP ==========
function checkESPStatus() {
    const alertDiv = document.getElementById('esp-status-alert');
    const badgeDiv = document.getElementById('esp-status-badge');
    const offlineTimeSpan = document.getElementById('esp-offline-time');
    
    if (!alertDiv || !badgeDiv) return;
    
    const updateLastUpdateText = (timestamp) => {
        const formatted = formatTimestamp(timestamp);
        document.querySelectorAll('.esp-last-update-text').forEach(span => {
            span.textContent = formatted;
        });
    };
    
    updateRelayStatus();
    updateHeapStatus();
    
    if (appState.heartbeat && appState.heartbeat.heartbeat_timestamp) {
        const lastTimestamp = appState.heartbeat.heartbeat_timestamp;
        const diffMs = calculateTimeDifference(lastTimestamp);
        
        updateLastUpdateText(lastTimestamp);
        
        if (diffMs > ESP_OFFLINE_THRESHOLD) {
            alertDiv.classList.remove('hidden');
            badgeDiv.classList.add('hidden');
            if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
        } else {
            alertDiv.classList.add('hidden');
            badgeDiv.classList.remove('hidden');
        }
        return;
    }
    
    let lastTimestamp = null;
    
    if (appState.readings && appState.readings.length > 0) {
        const lastReading = appState.readings[appState.readings.length - 1];
        lastTimestamp = lastReading.reading_timestamp;
    } else if (appState.controller && appState.controller.state_timestamp) {
        lastTimestamp = appState.controller.state_timestamp;
    }
    
    if (!lastTimestamp) {
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = 'sem dados';
        updateLastUpdateText(new Date().toISOString());
        return;
    }
    
    const diffMs = calculateTimeDifference(lastTimestamp);
    
    updateLastUpdateText(lastTimestamp);
    
    if (diffMs > ESP_OFFLINE_THRESHOLD) {
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
    } else {
        alertDiv.classList.add('hidden');
        badgeDiv.classList.remove('hidden');
    }
}

// ========== STATUS MEMÓRIA HEAP ==========
function updateHeapStatus() {
    const relayContainer = document.getElementById('relay-status');
    if (!relayContainer) return;
    
    // Remove alerta existente se houver
    let existingHeapAlert = document.getElementById('heap-status-alert');
    
    // Busca free_heap do heartbeat
    const freeHeap = appState.heartbeat?.free_heap;
    
    // **CORREÇÃO AQUI**: Alerta deve aparecer APENAS quando freeHeap for BAIXO
    // Se não tem dado ou está saudável (> 30KB), esconde o alerta
    if (!freeHeap || freeHeap > HEAP_WARNING_THRESHOLD) {
        if (existingHeapAlert) {
            existingHeapAlert.classList.add('hidden');
        }
        return;
    }
    
    // **CORREÇÃO AQUI**: Cria/mostra alerta APENAS quando a memória está baixa
    if (freeHeap <= HEAP_WARNING_THRESHOLD) {
        // Cria o elemento se não existir
        if (!existingHeapAlert) {
            existingHeapAlert = document.createElement('div');
            existingHeapAlert.id = 'heap-status-alert';
            existingHeapAlert.className = 'flex items-center gap-1 text-sm';
            relayContainer.appendChild(existingHeapAlert);
        }
        
        // Formata o valor em KB
        const heapKB = (freeHeap / 1024).toFixed(1);
        
        // Determina se é crítico ou apenas atenção
        const isCritical = freeHeap < HEAP_CRITICAL_THRESHOLD;
        
        if (isCritical) {
            existingHeapAlert.innerHTML = `
                <i class="fas fa-exclamation-triangle text-red-600"></i>
                <span class="font-semibold text-red-700">
                    Memória crítica: ${heapKB}KB
                </span>
            `;
            existingHeapAlert.title = 'Memória do ESP8266 muito baixa! Risco de travamento. Considere reiniciar o dispositivo.';
        } else {
            existingHeapAlert.innerHTML = `
                <i class="fas fa-exclamation-circle text-yellow-600"></i>
                <span class="font-semibold text-yellow-700">
                    Memória baixa: ${heapKB}KB
                </span>
            `;
            existingHeapAlert.title = 'Memória do ESP8266 abaixo do ideal. Monitore para possíveis problemas.';
        }
        
        existingHeapAlert.classList.remove('hidden');
        
        if (DEBUG_MODE) {
            console.log(`⚠️ Heap ${isCritical ? 'CRÍTICO' : 'baixo'}: ${heapKB}KB (${freeHeap} bytes)`);
        }
    } else {
        // **CORREÇÃO AQUI**: Se a memória estiver OK (> 30KB), esconde o alerta
        if (existingHeapAlert) {
            existingHeapAlert.classList.add('hidden');
        }
    }
}

function updateRelayStatus() {
    const coolerStatusDiv = document.getElementById('cooler-status');
    const heaterStatusDiv = document.getElementById('heater-status');
    const relayContainer = document.getElementById('relay-status');
    
    if (!coolerStatusDiv || !heaterStatusDiv || !relayContainer) return;
    
    let coolerActive = false;
    let heaterActive = false;
    let waitingStatus = null;
    
    // ✅ Fonte 1: espState (fermentation_states) - dados mais completos do ESP
    // O ESP envia cooling/heating no estado completo a cada 30s
    if (appState.espState) {
        coolerActive = appState.espState.cooling === true || appState.espState.cooling === 1;
        heaterActive = appState.espState.heating === true || appState.espState.heating === 1;
        
        if (appState.espState.control_status) {
            const cs = appState.espState.control_status;
            if (cs.is_waiting && cs.wait_reason) {
                waitingStatus = {
                    reason: cs.wait_reason,
                    display: cs.wait_display || 'aguardando'
                };
            }
        }
    }
    
    // ✅ Fonte 2: controller_states - tabela dedicada de controle
    if (!coolerActive && !heaterActive && appState.controller) {
        coolerActive = appState.controller.cooling === 1 || appState.controller.cooling === true;
        heaterActive = appState.controller.heating === 1 || appState.controller.heating === true;
    }
    
    // ✅ Fonte 3: heartbeat control_status (para waiting status)
    if (appState.heartbeat && appState.heartbeat.control_status) {
        const cs = appState.heartbeat.control_status;
        
        if (!waitingStatus && cs.is_waiting && cs.wait_reason) {
            waitingStatus = {
                reason: cs.wait_reason,
                display: cs.wait_display || 'aguardando'
            };
        }
    }
    
    if (coolerActive) {
        coolerStatusDiv.classList.remove('hidden');
    } else {
        coolerStatusDiv.classList.add('hidden');
    }
    
    if (heaterActive) {
        heaterStatusDiv.classList.remove('hidden');
    } else {
        heaterStatusDiv.classList.add('hidden');
    }
    
    let existingWaitDiv = document.getElementById('waiting-status');
    
    if (waitingStatus) {
        if (!existingWaitDiv) {
            existingWaitDiv = document.createElement('div');
            existingWaitDiv.id = 'waiting-status';
            existingWaitDiv.className = 'flex items-center gap-1 text-sm';
            relayContainer.appendChild(existingWaitDiv);
        }
        
        existingWaitDiv.innerHTML = `
            <i class="fas fa-hourglass-half text-yellow-600"></i>
            <span class="font-semibold text-yellow-700">
                ${waitingStatus.reason} (${waitingStatus.display})
            </span>
        `;
        existingWaitDiv.classList.remove('hidden');
    } else {
        if (existingWaitDiv) {
            existingWaitDiv.classList.add('hidden');
        }
    }
}

// ========== TEMPLATES ==========
const cardTemplate = ({ title, icon, value, subtitle, subtitleStyle = '', color }) => `
    <div class="card">
        <div class="flex items-center gap-3 mb-2">
            <i class="${icon}" style="color: ${color}; font-size: 1.5rem;"></i>
            <span class="text-sm font-medium text-gray-600">${title}</span>
        </div>
        <div class="text-3xl font-bold text-gray-800">
            ${value}
        </div>
        ${subtitle ? `<div class="text-sm text-gray-600 mt-1"${subtitleStyle ? ` style="${subtitleStyle}"` : ''}>${subtitle}</div>` : ''}
    </div>
`;

const stageTemplate = (stage, index, isCurrent, isCompleted) => {
    let borderColor = 'border-gray-200';
    let bgColor = 'bg-gray-50';
    let statusText = 'Aguardando';
    
    if (isCurrent) {
        borderColor = 'border-blue-500';
        bgColor = 'bg-blue-50';
        statusText = 'Em andamento';
    } else if (isCompleted) {
        borderColor = 'border-green-500';
        bgColor = 'bg-green-50';
        statusText = 'Concluída';
    }
    
    return `
    <div class="p-4 rounded-lg border-2 ${borderColor} ${bgColor}">
        <div class="flex justify-between items-start">
            <div>
                <h3 class="font-semibold text-gray-800">
                    ${stage.type === 'ramp' ? '<i class="fas fa-chart-line text-blue-600 mr-2"></i>' : ''}
                    Etapa ${index + 1}
                    ${isCurrent ? `<span class="ml-2 text-sm text-blue-600">(${statusText})</span>` : ''}
                    ${isCompleted ? '<span class="ml-2 text-sm text-green-600">(Concluída)</span>' : ''}
                </h3>
                <p class="text-sm text-gray-600 mt-1">
                    ${getStageDescription(stage)}
                </p>
            </div>
        </div>
    </div>
    `;
};

function getStageDescription(stage) {
    const formatDurationDisplay = (days) => {
        if (days >= 1) {
            return days % 1 === 0 ? `${days} dias` : `${days.toFixed(1)} dias`;
        } else {
            const totalHours = days * 24;
            const hours = Math.floor(totalHours);
            const minutes = Math.round((totalHours - hours) * 60);
            
            if (hours === 0 && minutes === 0) {
                return "menos de 1 minuto";
            } else if (hours === 0) {
                return `${minutes} minuto${minutes !== 1 ? 's' : ''}`;
            } else if (minutes === 0) {
                return `${hours} hora${hours !== 1 ? 's' : ''}`;
            } else {
                return `${hours} hora${hours !== 1 ? 's' : ''} e ${minutes} minuto${minutes !== 1 ? 's' : ''}`;
            }
        }
    };

    switch(stage.type) {
        case 'temperature':
            return `${stage.target_temp}°C por ${formatDurationDisplay(stage.duration)}`;
        case 'gravity':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG`;
        case 'gravity_time':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG (máx ${formatDurationDisplay(stage.max_duration)})`;
        case 'ramp':
            const direction = stage.direction === 'up' ? '▲' : '▼';
            
            let rampTimeDisplay;
            const rampDays = stage.ramp_time / 24;
            
            if (rampDays >= 1) {
                rampTimeDisplay = rampDays % 1 === 0 ? `${rampDays} dias` : `${rampDays.toFixed(1)} dias`;
            } else {
                const hours = stage.ramp_time;
                const minutes = Math.round((hours - Math.floor(hours)) * 60);
                
                if (hours === 0 && minutes === 0) {
                    rampTimeDisplay = "menos de 1 minuto";
                } else if (Math.floor(hours) === 0) {
                    rampTimeDisplay = `${minutes} minuto${minutes !== 1 ? 's' : ''}`;
                } else if (minutes === 0) {
                    rampTimeDisplay = `${Math.floor(hours)} hora${Math.floor(hours) !== 1 ? 's' : ''}`;
                } else {
                    rampTimeDisplay = `${Math.floor(hours)} hora${Math.floor(hours) !== 1 ? 's' : ''} e ${minutes} minuto${minutes !== 1 ? 's' : ''}`;
                }
            }
            
            return `${direction} ${stage.start_temp}°C → ${stage.target_temp}°C em ${rampTimeDisplay}`;
        default:
            return '';
    }
}

// ========== RENDERIZAÇÃO ==========
function renderUI() {
    if (DEBUG_MODE) {
        console.log('🔍 RenderUI chamada', {
            temConfig: !!appState.config,
            temStages: appState.config?.stages?.length || 0,
            temEspState: !!appState.espState,
            timeRemaining: appState.espState?.timeRemaining,
            fermentationCompleted: appState.espState?.fermentationCompleted
        });
    }
    
    const noFermentationCard = document.getElementById('no-fermentation-card');
    if (noFermentationCard) {
        noFermentationCard.style.display = 'none';
    }
    
    if (!appState.config || !appState.config.stages || appState.config.stages.length === 0) {
        renderNoActiveFermentation();
        return;
    }

    const nameElement = document.getElementById('fermentation-name');
    const stageElement = document.getElementById('stage-info');
    
    if (nameElement) nameElement.textContent = appState.config.name || 'Sem nome';
    
    if (stageElement) {
        const currentStage = (appState.config.current_stage_index || 0) + 1;
        const totalStages = appState.config.stages.length;
        
        if (appState.espState?.fermentationCompleted || 
            appState.espState?.timeRemaining?.status === 'completed') {
            stageElement.textContent = `Todas as ${totalStages} etapas concluídas`;
        } else {
            stageElement.textContent = `Etapa ${currentStage} de ${totalStages}`;
        }
    }
    
    const timeElement = document.getElementById('time-remaining');
    if (timeElement && appState.espState) {
        const tr = appState.espState.timeRemaining;
        const targetReached = appState.espState.targetReached === true;
        const fermentationCompleted = appState.espState.fermentationCompleted === true;
        
        if (DEBUG_MODE) {
            console.log('⏱️ Time Element Debug:', {
                hasTimeRemaining: !!tr,
                targetReached: targetReached,
                fermentationCompleted: fermentationCompleted,
                timeRemaining: tr
            });
        }
        
        if (fermentationCompleted || tr?.status === 'completed' || tr?.unit === 'completed') {
            timeElement.innerHTML = `
                <i class="fas fa-check-circle text-green-600"></i> 
                <span class="text-green-600 font-semibold">
                    Fermentação concluída
                </span>
            `;
            timeElement.style.display = 'flex';
            if (DEBUG_MODE) {
                console.log('🎨 Time element: Fermentação concluída');
            }
        }
        else if (tr && targetReached) {
            let icon = 'fas fa-hourglass-half';
            let statusClass = 'text-green-600';
            
            if (tr.status === 'waiting_gravity') {
                icon = 'fas fa-hourglass-start';
                statusClass = 'text-blue-600';
            }
            
            const timeDisplay = formatTimeRemaining(tr);
            
            let statusText = '';
            if (tr.status !== 'waiting_gravity' && tr.unit !== 'indefinite' && tr.unit !== 'ind') {
                statusText = ' restantes';
            }
            
            timeElement.innerHTML = `
                <i class="${icon} ${statusClass}"></i> 
                <span class="${statusClass}">
                    ${timeDisplay}${statusText}
                </span>
            `;
            timeElement.style.display = 'flex';
            
            if (DEBUG_MODE) {
                console.log(`🎨 Time element (targetReached=true): ${timeDisplay}${statusText}`);
            }
        } else if (targetReached === false) {
            timeElement.innerHTML = `
                <i class="fas fa-hourglass-start text-yellow-600"></i>
                <span class="text-yellow-600">
                    Aguardando temperatura alvo
                </span>
            `;
            timeElement.style.display = 'flex';
            if (DEBUG_MODE) {
                console.log('🎨 Time element (targetReached=false): Aguardando temperatura alvo');
            }
        } else {
            timeElement.style.display = 'none';
        }
    }
    
    checkESPStatus();
    renderInfoCards();
    renderChart();
    renderStagesList();
}

function renderInfoCards() {
    const infoCards = document.getElementById('info-cards');
    if (!infoCards) return;
    
    const lastReading = appState.readings[appState.readings.length - 1] || {};
    const currentStage = appState.config.stages[appState.config.current_stage_index] || {};
    
    const currentTemp = parseFloat(lastReading.temp_fermenter) || 0;
    const fridgeTemp = parseFloat(lastReading.temp_fridge) || 0;
    
    const targetTemp = parseFloat(lastReading.temp_target) ||
                       parseFloat(currentStage.target_temp) || 0;
    
    let tempStatus = '';
    let tempColor = '#9ca3af';
    
    if (!isNaN(currentTemp) && !isNaN(targetTemp) && currentTemp !== null && targetTemp !== null) {
        const diff = Math.abs(currentTemp - targetTemp);
        if (diff <= 0.5) {
            tempStatus = '✅ No alvo';
            tempColor = '#10b981';
        } else if (currentTemp < targetTemp) {
            tempStatus = '⬇️ Abaixo do alvo';
            tempColor = '#3b82f6';
        } else {
            tempStatus = '⬆️ Acima do alvo';
            tempColor = '#ef4444';
        }
    }

    const ispindelData = getIspindelData();
    
    let spindelSubtitle = '';
    let spindelSubtitleColor = '';
    
    if (ispindelData.isStale && ispindelData.staleMessage) {
        spindelSubtitle = `⚠️ ${ispindelData.staleMessage}`;
        spindelSubtitleColor = 'color: #f59e0b;';
    } else if (ispindelData.temperature || ispindelData.battery) {
        const parts = [];
        if (ispindelData.temperature) parts.push(`${ispindelData.temperature.toFixed(1)}°C`);
        if (ispindelData.battery) parts.push(`${ispindelData.battery.toFixed(2)}V`);
        spindelSubtitle = parts.join(' • ');
    }

    const targetGravity = (currentStage.type === 'gravity' || currentStage.type === 'gravity_time') 
        ? parseFloat(currentStage.target_gravity) || 0 
        : 0;
    
    let gravityTargetSubtitle = '';
    if (targetGravity > 0 && ispindelData.gravity) {
        const diff = ispindelData.gravity - targetGravity;
        if (Math.abs(diff) < 0.001) {
            gravityTargetSubtitle = '✅ No alvo';
        } else if (diff > 0) {
            gravityTargetSubtitle = `⬇️ ${Math.abs(diff).toFixed(3)} acima`;
        } else {
            gravityTargetSubtitle = `⬆️ ${Math.abs(diff).toFixed(3)} abaixo`;
        }
    } else if (targetGravity > 0) {
        gravityTargetSubtitle = 'Aguardando leitura';
    } else {
        gravityTargetSubtitle = 'Sem alvo definido';
    }

    const fermentationCompleted = appState.espState?.fermentationCompleted === true ||
                                   appState.espState?.timeRemaining?.status === 'completed';
    const isReallyRunning = appState.espState?.targetReached === true;
    
    let countingStatus, countingColor;
    
    if (fermentationCompleted) {
        countingStatus = '✅ Fermentação concluída';
        countingColor = '#10b981';
    } else if (isReallyRunning) {
        countingStatus = '✅ Contagem iniciada';
        countingColor = '#10b981';
    } else {
        countingStatus = '⏳ Aguardando alvo';
        countingColor = '#f59e0b';
    }

    infoCards.innerHTML = `
        ${cardTemplate({
            title: 'Temp. Fermentador',
            icon: 'fas fa-thermometer-full',
            value: !isNaN(currentTemp) && currentTemp !== null ? `${currentTemp.toFixed(1)}°C` : '--',
            subtitle: tempStatus,
            color: tempColor
        })}
        ${cardTemplate({
            title: 'Temp. Geladeira',
            icon: 'fas fa-thermometer-half',
            value: !isNaN(fridgeTemp) && fridgeTemp !== null ? `${fridgeTemp.toFixed(1)}°C` : '--',
            subtitle: 'Sensor ambiente',
            color: '#3b82f6'
        })}
        ${cardTemplate({
            title: 'Temperatura Alvo',
            icon: 'fas fa-crosshairs',
            value: !isNaN(targetTemp) && targetTemp !== null ? `${targetTemp.toFixed(1)}°C` : '--',
            subtitle: countingStatus,
            color: countingColor
        })}
        ${cardTemplate({
            title: 'Gravidade Atual',
            icon: 'fas fa-tint',
            value: ispindelData.gravity ? ispindelData.gravity.toFixed(3) : '--',
            subtitle: spindelSubtitle,
            subtitleStyle: spindelSubtitleColor,
            color: ispindelData.isStale ? '#f59e0b' : '#10b981'
        })}
        ${cardTemplate({
            title: 'Gravidade Alvo',
            icon: 'fas fa-bullseye',
            value: targetGravity > 0 ? targetGravity.toFixed(3) : '--',
            subtitle: gravityTargetSubtitle,
            color: targetGravity > 0 ? '#8b5cf6' : '#9ca3af'
        })}
    `;

    if (appState.heartbeat && appState.heartbeat.control_status) {
        const cs = appState.heartbeat.control_status;
        const coolerActive = appState.heartbeat.cooler_active === 1 || appState.heartbeat.cooler_active === true;
        const heaterActive = appState.heartbeat.heater_active === 1 || appState.heartbeat.heater_active === true;
        
        let statusIcon = 'fas fa-check-circle';
        let statusText = 'Sistema Estável';
        let statusColor = '#10b981';
        let statusSubtitle = '';
        
        if (cs.is_waiting && cs.wait_reason) {
            statusIcon = 'fas fa-hourglass-half';
            statusText = 'Aguardando';
            statusSubtitle = cs.wait_reason;
            if (cs.wait_display) {
                statusSubtitle += ` (${cs.wait_display})`;
            }
            statusColor = '#f59e0b';
        } else if (coolerActive) {
            statusIcon = 'fas fa-snowflake';
            statusText = 'Resfriando';
            statusColor = '#3b82f6';
        } else if (heaterActive) {
            statusIcon = 'fas fa-fire';
            statusText = 'Aquecendo';
            statusColor = '#ef4444';
        }
    }
}

function renderStagesList() {
    const stagesList = document.getElementById('stages-list');
    if (!stagesList || !appState.config) return;
    
    const currentIndex = appState.config.current_stage_index;
    const fermentationCompleted = appState.espState?.fermentationCompleted === true ||
                                   appState.espState?.timeRemaining?.status === 'completed';
    
    stagesList.innerHTML = appState.config.stages
        .map((stage, index) => {
            const isCurrent = !fermentationCompleted && index === currentIndex;
            const isCompleted = fermentationCompleted || stage.status === 'completed' || index < currentIndex;
            
            return stageTemplate(stage, index, isCurrent, isCompleted);
        })
        .join('');
}

// ========== GRÁFICO ==========
function renderChart() {
    const canvas = document.getElementById('fermentation-chart');
    const ctx = canvas ? canvas.getContext('2d') : null;
    const noDataMsg = document.getElementById('no-data-message');

    if (!ctx || !appState.config || appState.readings.length === 0) {
        if (canvas) canvas.style.display = 'none';
        if (noDataMsg) noDataMsg.style.display = 'block';
        return;
    }

    if (canvas) canvas.style.display = 'block';
    if (noDataMsg) noDataMsg.style.display = 'none';

    if (chart) {
        chart.destroy();
    }

    const labels = appState.readings.map(r => {
        const date = utcToSaoPaulo(r.reading_timestamp);
        const day = date.getDate().toString().padStart(2, '0');
        const month = (date.getMonth() + 1).toString().padStart(2, '0');
        const hour = date.getHours().toString().padStart(2, '0');
        const minute = date.getMinutes().toString().padStart(2, '0');
        return `${day}/${month} ${hour}:${minute}`;
    });

    const coolerData = [];
    const heaterData = [];
    
    if (appState.controllerHistory && appState.controllerHistory.length > 0) {
        appState.readings.forEach((reading, idx) => {
            const readingTime = new Date(reading.reading_timestamp).getTime();
            
            const controllerState = appState.controllerHistory.find(cs => {
                const stateTime = new Date(cs.state_timestamp).getTime();
                const diff = Math.abs(stateTime - readingTime);
                return diff < 300000;
            });
            
            if (controllerState) {
                const coolerActive = controllerState.cooling === 1 || controllerState.cooling === true;
                const heaterActive = controllerState.heating === 1 || controllerState.heating === true;
                
                coolerData.push(coolerActive ? parseFloat(reading.temp_fridge) : null);
                heaterData.push(heaterActive ? parseFloat(reading.temp_fridge) : null);
            } else {
                coolerData.push(null);
                heaterData.push(null);
            }
        });
    }

    const gravityMap = new Map();
    if (appState.ispindelReadings && appState.ispindelReadings.length > 0) {
        appState.ispindelReadings.forEach(ir => {
            const timestamp = new Date(ir.reading_timestamp).getTime();
            gravityMap.set(timestamp, parseFloat(ir.gravity));
        });
    }
    
    const gravityData = appState.readings.map(r => {
        const readingTime = new Date(r.reading_timestamp).getTime();
        
        let closestGravity = null;
        let closestDiff = Infinity;
        
        gravityMap.forEach((gravity, timestamp) => {
            const diff = Math.abs(timestamp - readingTime);
            if (diff < closestDiff && diff < 1800000) {
                closestDiff = diff;
                closestGravity = gravity;
            }
        });
        
        return closestGravity;
    });

    const datasets = [
        {
            label: 'Temp. Geladeira',
            data: appState.readings.map(r => parseFloat(r.temp_fridge)),
            borderColor: '#3b82f6',
            backgroundColor: 'rgba(59, 130, 246, 0.1)',
            tension: 0.4,
            fill: false,
            order: 2
        },
        {
            label: 'Temp. Fermentador',
            data: appState.readings.map(r => parseFloat(r.temp_fermenter)),
            borderColor: '#1e40af',
            backgroundColor: 'rgba(30, 64, 175, 0.1)',
            tension: 0.4,
            fill: false,
            order: 2
        },
        {
            label: 'Temperatura Alvo',
            data: appState.readings.map(r => parseFloat(r.temp_target)),
            borderColor: '#ef4444',
            borderDash: [5, 5],
            backgroundColor: 'rgba(239, 68, 68, 0.1)',
            tension: 0.4,
            fill: false,
            pointRadius: 0,
            order: 2
        },
        {
            label: 'Gravidade',
            data: gravityData,
            borderColor: '#10b981',
            backgroundColor: 'rgba(16, 185, 129, 0.1)',
            tension: 0.4,
            fill: false,
            yAxisID: 'y1',
            order: 2,
            spanGaps: true
        }
    ];

    if (coolerData.some(v => v !== null)) {
        datasets.push({
            label: '❄️ Cooler Ativo',
            data: coolerData,
            backgroundColor: 'rgba(59, 130, 246, 0.25)',
            borderColor: 'rgba(59, 130, 246, 0.5)',
            borderWidth: 1,
            fill: true,
            pointRadius: 0,
            tension: 0,
            order: 1
        });
    }

    if (heaterData.some(v => v !== null)) {
        datasets.push({
            label: '🔥 Heater Ativo',
            data: heaterData,
            backgroundColor: 'rgba(239, 68, 68, 0.25)',
            borderColor: 'rgba(239, 68, 68, 0.5)',
            borderWidth: 1,
            fill: true,
            pointRadius: 0,
            tension: 0,
            order: 1
        });
    }

    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: datasets
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: 'index',
                intersect: false
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    labels: {
                        usePointStyle: true,
                        padding: 15,
                        font: { size: 11 }
                    }
                },
                tooltip: {
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            
                            if (label.includes('Cooler Ativo') || label.includes('Heater Ativo')) {
                                return context.parsed.y !== null ? label : null;
                            }
                            
                            if (label) label += ': ';
                            if (context.parsed.y !== null) {
                                if (context.dataset.yAxisID === 'y1') {
                                    label += context.parsed.y.toFixed(3);
                                } else {
                                    label += context.parsed.y.toFixed(1) + '°C';
                                }
                            }
                            return label;
                        }
                    }
                }
            },
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: { display: true, text: 'Temperatura (°C)' }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: { display: true, text: 'Gravidade' },
                    grid: { drawOnChartArea: false },
                    ticks: {
                        callback: function(value) {
                            return value.toFixed(3);
                        }
                    }
                }
            }
        }
    });
}

function renderNoActiveFermentation() {
    const nameElement = document.getElementById('fermentation-name');
    const stageElement = document.getElementById('stage-info');
    const timeElement = document.getElementById('time-remaining');
    
    if (nameElement) nameElement.textContent = 'Nenhuma fermentação';
    if (stageElement) stageElement.textContent = 'Inicie uma fermentação para monitorar';
    if (timeElement) timeElement.style.display = 'none';
    
    // Pega dados reais dos sensores (se disponíveis)
    const lastReading = appState.latestReading || {};
    const ispindelData = getIspindelData();
    
    const currentTemp = parseFloat(lastReading.temp_fermenter) || null;
    const fridgeTemp = parseFloat(lastReading.temp_fridge) || null;
    
    // Monta o subtitle do iSpindel
    let spindelSubtitle = '';
    let spindelSubtitleColor = '';
    
    if (ispindelData.gravity) {
        if (ispindelData.isStale && ispindelData.staleMessage) {
            spindelSubtitle = `⚠️ ${ispindelData.staleMessage}`;
            spindelSubtitleColor = 'color: #f59e0b;';
        } else if (ispindelData.temperature || ispindelData.battery) {
            const parts = [];
            if (ispindelData.temperature) parts.push(`${ispindelData.temperature.toFixed(1)}°C`);
            if (ispindelData.battery) parts.push(`${ispindelData.battery.toFixed(2)}V`);
            spindelSubtitle = parts.join(' • ');
        }
    } else {
        spindelSubtitle = 'Sem dados do iSpindel';
    }
    
    const infoCards = document.getElementById('info-cards');
    if (infoCards) {
        infoCards.innerHTML = `
            ${cardTemplate({
                title: 'Temp. Fermentador',
                icon: 'fas fa-thermometer-full',
                value: currentTemp !== null ? `${currentTemp.toFixed(1)}°C` : '--',
                subtitle: currentTemp !== null ? 'Leitura atual' : 'Sem dados',
                color: currentTemp !== null ? '#10b981' : '#9ca3af'
            })}
            ${cardTemplate({
                title: 'Temp. Geladeira',
                icon: 'fas fa-thermometer-half',
                value: fridgeTemp !== null ? `${fridgeTemp.toFixed(1)}°C` : '--',
                subtitle: fridgeTemp !== null ? 'Leitura atual' : 'Sem dados',
                color: fridgeTemp !== null ? '#3b82f6' : '#9ca3af'
            })}
            ${cardTemplate({
                title: 'Temperatura Alvo',
                icon: 'fas fa-crosshairs',
                value: '--',
                subtitle: 'Sem fermentação ativa',
                color: '#9ca3af'
            })}
            ${cardTemplate({
                title: 'Gravidade Atual',
                icon: 'fas fa-tint',
                value: ispindelData.gravity ? ispindelData.gravity.toFixed(3) : '--',
                subtitle: spindelSubtitle,
                subtitleStyle: spindelSubtitleColor,
                color: ispindelData.gravity ? (ispindelData.isStale ? '#f59e0b' : '#10b981') : '#9ca3af'
            })}
            ${cardTemplate({
                title: 'Gravidade Alvo',
                icon: 'fas fa-bullseye',
                value: '--',
                subtitle: 'Sem alvo definido',
                color: '#9ca3af'
            })}
        `;
    }
    
    const canvas = document.getElementById('fermentation-chart');
    const noDataMsg = document.getElementById('no-data-message');
    const chartContainer = canvas ? canvas.parentElement : null;
    
    if (canvas) canvas.style.display = 'none';
    if (noDataMsg) noDataMsg.style.display = 'none';
    
    let noFermentationCard = document.getElementById('no-fermentation-card');
    
    if (!noFermentationCard && chartContainer) {
        noFermentationCard = document.createElement('div');
        noFermentationCard.id = 'no-fermentation-card';
        chartContainer.appendChild(noFermentationCard);
    }
    
    if (noFermentationCard) {
        noFermentationCard.className = 'flex flex-col items-center justify-center py-16';
        noFermentationCard.innerHTML = `
            <i class="fas fa-chart-line" style="font-size: 4rem; color: #9ca3af; margin-bottom: 1rem;"></i>
            <h2 class="text-2xl font-semibold text-gray-700 mb-2">Nenhuma Fermentação Ativa</h2>
            <p class="text-gray-500 mb-6 text-center max-w-md">
                Configure e inicie uma fermentação para visualizar o gráfico de temperatura e gravidade aqui.
            </p>
            <button onclick="location.href='config.html'" class="btn btn-primary flex items-center gap-2">
                <i class="fas fa-cog"></i> Ir para Configuração
            </button>
        `;
        noFermentationCard.style.display = 'flex';
    }
    
    const stagesList = document.getElementById('stages-list');
    if (stagesList) {
        stagesList.innerHTML = `
            <div class="p-4 rounded-lg border-2 border-gray-200 bg-gray-50 text-center">
                <p class="text-gray-500">
                    <i class="fas fa-info-circle mr-2"></i>
                    Nenhuma etapa configurada. Inicie uma fermentação para ver as etapas.
                </p>
            </div>
        `;
    }
    
    const alertDiv = document.getElementById('esp-status-alert');
    const badgeDiv = document.getElementById('esp-status-badge');
    if (alertDiv) alertDiv.classList.add('hidden');
    if (badgeDiv) badgeDiv.classList.add('hidden');
    
    const coolerStatusDiv = document.getElementById('cooler-status');
    const heaterStatusDiv = document.getElementById('heater-status');
    const waitingStatusDiv = document.getElementById('waiting-status');
    const heapStatusDiv = document.getElementById('heap-status-alert');
    if (coolerStatusDiv) coolerStatusDiv.classList.add('hidden');
    if (heaterStatusDiv) heaterStatusDiv.classList.add('hidden');
    if (waitingStatusDiv) waitingStatusDiv.classList.add('hidden');
    if (heapStatusDiv) heapStatusDiv.classList.add('hidden');
}

// ========== INICIALIZAÇÃO ==========
async function initAppAfterAuth() {
    try {
        await loadCompleteState();
        
        if (refreshInterval) {
            clearInterval(refreshInterval);
        }
        
        refreshInterval = setInterval(autoRefreshData, REFRESH_INTERVAL);
    } catch (error) {
        console.error('Erro ao inicializar:', error);
        renderNoActiveFermentation();
    }
}

window.login = login;
window.logout = logout;
window.refreshData = loadCompleteState;

document.addEventListener('DOMContentLoaded', async () => {
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
    
    await checkAuthStatus();
});