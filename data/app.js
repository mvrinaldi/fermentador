// app.js - Monitor Passivo (ESP como fonte de verdade) + Detecção de Offline
const API_BASE_URL = '/api.php?path=';

// ========== CONSTANTES DE FUSO HORÁRIO ==========
const SAO_PAULO_TIMEZONE = 'America/Sao_Paulo';

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;

const REFRESH_INTERVAL = 30000; // 30 segundos
const ESP_OFFLINE_THRESHOLD = 120000; // 2 minutos sem dados = offline

// ========== ESTADO DA APLICAÇÃO (recebido do servidor) ==========
let appState = {
    config: null,
    espState: null,
    readings: [],
    ispindel: null,
    controller: null,
    heartbeat: null,
    lastUpdate: null
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
        console.error('Erro na API:', error);
        throw error;
    }
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
            controller: null,
            lastUpdate: null
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
        console.error('Erro ao fazer logout:', error);
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
        console.error('Erro ao verificar autenticação:', error);
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

// ========== FUNÇÕES DE DATA/HORA SIMPLIFICADAS ==========

// Converte timestamp UTC para horário de São Paulo
function utcToSaoPaulo(dateString) {
    if (!dateString) {
        // Retorna data atual em UTC
        return new Date();
    }
    
    let date;
    
    if (dateString instanceof Date) {
        date = dateString;
    } else {
        // Garante que a data seja interpretada como UTC
        let normalized = dateString;
        if (!dateString.endsWith('Z') && dateString.includes('T')) {
            normalized = dateString + 'Z';
        }
        date = new Date(normalized);
        
        if (isNaN(date.getTime())) {
            return new Date(); // Fallback
        }
    }
    
    // Ajusta -3 horas para São Paulo (UTC-3)
    const SAO_PAULO_OFFSET = -3 * 60 * 60 * 1000; // -3 horas em milissegundos
    return new Date(date.getTime() + SAO_PAULO_OFFSET);
}

// Calcula diferença de tempo considerando fuso horário
function calculateTimeDifference(utcTimestamp) {
    if (!utcTimestamp) return Infinity;
    
    const nowUTC = new Date();
    const timestampUTC = new Date(utcTimestamp);
    
    // Ambos já estão em UTC, então a diferença é direta
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

// Formata timestamp para exibição em São Paulo
function formatTimestamp(date) {
    if (!date) return '--:--';
    
    const saoPauloDate = utcToSaoPaulo(date);
    
    // Formata manualmente para garantir consistência
    const day = saoPauloDate.getDate().toString().padStart(2, '0');
    const month = (saoPauloDate.getMonth() + 1).toString().padStart(2, '0');
    const year = saoPauloDate.getFullYear();
    const hour = saoPauloDate.getHours().toString().padStart(2, '0');
    const minute = saoPauloDate.getMinutes().toString().padStart(2, '0');
    
    return `${day}/${month}/${year} ${hour}:${minute}`;
}

// Para debug
function debugTimezone(dateString) {
    console.log('=== DEBUG TIMEZONE ===');
    console.log('Input:', dateString);
    
    const dateUTC = new Date(dateString);
    console.log('Interpreted as UTC:', dateUTC.toISOString());
    console.log('UTC Hours:', dateUTC.getUTCHours());
    
    const saoPauloDate = utcToSaoPaulo(dateString);
    console.log('São Paulo Hours:', saoPauloDate.getHours());
    console.log('Formatted SP:', formatTimestamp(dateString));
    console.log('=====================');
}

// ========== DETECÇÃO DE ESP OFFLINE ==========
function checkESPStatus() {
    const alertDiv = document.getElementById('esp-status-alert');
    const badgeDiv = document.getElementById('esp-status-badge');
    const offlineTimeSpan = document.getElementById('esp-offline-time');
    
    if (!alertDiv || !badgeDiv) return;
    
    // Função para atualizar TODOS os timestamps
    const updateLastUpdateText = (timestamp) => {
        const formatted = formatTimestamp(timestamp);
        document.querySelectorAll('.esp-last-update-text').forEach(span => {
            span.textContent = formatted;
        });
    };
    
    // PRIORIDADE 1: Usa heartbeat se disponível
    if (appState.heartbeat && appState.heartbeat.heartbeat_timestamp) {
        const lastTimestamp = appState.heartbeat.heartbeat_timestamp;
        const diffMs = calculateTimeDifference(lastTimestamp);
        
        updateLastUpdateText(lastTimestamp);
        
        if (diffMs > ESP_OFFLINE_THRESHOLD) {
            // ESP OFFLINE
            alertDiv.classList.remove('hidden');
            badgeDiv.classList.add('hidden');
            if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
        } else {
            // ESP ONLINE
            alertDiv.classList.add('hidden');
            badgeDiv.classList.remove('hidden');// app.js - Monitor Passivo (ESP como fonte de verdade) + Detecção de Offline
const API_BASE_URL = '/api.php?path=';

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;

const REFRESH_INTERVAL = 30000; // 30 segundos
const ESP_OFFLINE_THRESHOLD = 120000; // 2 minutos sem dados = offline
const SAO_PAULO_UTC_OFFSET = -3 * 60 * 60 * 1000; // -3 horas em milissegundos

// ========== ESTADO DA APLICAÇÃO (recebido do servidor) ==========
let appState = {
    config: null,
    espState: null,
    readings: [],
    ispindel: null,
    controller: null,
    heartbeat: null,
    lastUpdate: null
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
        console.error('Erro na API:', error);
        throw error;
    }
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
            controller: null,
            lastUpdate: null
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
        console.error('Erro ao fazer logout:', error);
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
        console.error('Erro ao verificar autenticação:', error);
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

// ========== FUNÇÕES DE DATA/HORA SIMPLIFICADAS ==========

// Converte timestamp UTC para horário de São Paulo (abordagem direta)
function utcToSaoPaulo(dateString) {
    if (!dateString) {
        // Retorna data atual ajustada para SP
        const now = new Date();
        return new Date(now.getTime() + SAO_PAULO_UTC_OFFSET);
    }
    
    let date;
    
    if (dateString instanceof Date) {
        date = dateString;
    } else {
        // Adiciona 'Z' se for UTC mas não tem o indicador
        let normalized = dateString;
        if (!dateString.endsWith('Z') && dateString.includes('T')) {
            normalized = dateString + 'Z';
        }
        date = new Date(normalized);
        
        if (isNaN(date.getTime())) {
            // Fallback
            return new Date(new Date().getTime() + SAO_PAULO_UTC_OFFSET);
        }
    }
    
    // Ajusta -3 horas para São Paulo (UTC-3)
    return new Date(date.getTime() + SAO_PAULO_UTC_OFFSET);
}

// Calcula diferença de tempo considerando fuso horário de São Paulo
function calculateTimeDifference(utcTimestamp) {
    if (!utcTimestamp) return Infinity;
    
    const nowUTC = new Date();
    const timestampUTC = new Date(utcTimestamp);
    
    // Ambos já estão em UTC, então a diferença é direta
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

// Formata timestamp para exibição em São Paulo
function formatTimestamp(date) {
    if (!date) return '--:--';
    
    const saoPauloDate = utcToSaoPaulo(date);
    
    // Formata manualmente para garantir consistência
    const day = saoPauloDate.getDate().toString().padStart(2, '0');
    const month = (saoPauloDate.getMonth() + 1).toString().padStart(2, '0');
    const year = saoPauloDate.getFullYear();
    const hour = saoPauloDate.getHours().toString().padStart(2, '0');
    const minute = saoPauloDate.getMinutes().toString().padStart(2, '0');
    
    return `${day}/${month}/${year} ${hour}:${minute}`;
}

// ========== DETECÇÃO DE ESP OFFLINE ==========
function checkESPStatus() {
    const alertDiv = document.getElementById('esp-status-alert');
    const badgeDiv = document.getElementById('esp-status-badge');
    const offlineTimeSpan = document.getElementById('esp-offline-time');
    
    if (!alertDiv || !badgeDiv) return;
    
    // Função para atualizar TODOS os timestamps
    const updateLastUpdateText = (timestamp) => {
        const formatted = formatTimestamp(timestamp);
        document.querySelectorAll('.esp-last-update-text').forEach(span => {
            span.textContent = formatted;
        });
    };
    
    // PRIORIDADE 1: Usa heartbeat se disponível
    if (appState.heartbeat && appState.heartbeat.heartbeat_timestamp) {
        const lastTimestamp = appState.heartbeat.heartbeat_timestamp;
        const diffMs = calculateTimeDifference(lastTimestamp);
        
        updateLastUpdateText(lastTimestamp);
        
        if (diffMs > ESP_OFFLINE_THRESHOLD) {
            // ESP OFFLINE
            alertDiv.classList.remove('hidden');
            badgeDiv.classList.add('hidden');
            if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
        } else {
            // ESP ONLINE
            alertDiv.classList.add('hidden');
            badgeDiv.classList.remove('hidden');
        }
        return;
    }
    
    // FALLBACK: Verifica última leitura ou última atualização do controlador
    let lastTimestamp = null;
    
    if (appState.readings && appState.readings.length > 0) {
        const lastReading = appState.readings[appState.readings.length - 1];
        lastTimestamp = lastReading.reading_timestamp;
    } else if (appState.controller && appState.controller.state_timestamp) {
        lastTimestamp = appState.controller.state_timestamp;
    }
    
    if (!lastTimestamp) {
        // Sem dados ainda - mostra offline
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = 'sem dados';
        
        // Usa UTC atual
        const nowUTC = new Date().toISOString();
        updateLastUpdateText(nowUTC);
        
        return;
    }
    
    const diffMs = calculateTimeDifference(lastTimestamp);
    
    // Atualiza texto de última atualização
    updateLastUpdateText(lastTimestamp);
    
    if (diffMs > ESP_OFFLINE_THRESHOLD) {
        // ESP OFFLINE
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
    } else {
        // ESP ONLINE
        alertDiv.classList.add('hidden');
        badgeDiv.classList.remove('hidden');
    }
}

// ========== CARREGAMENTO DE DADOS (ENDPOINT UNIFICADO) ==========
async function loadCompleteState() {
    try {
        // 1. Verifica se há fermentação ativa
        const activeData = await apiRequest('active');
        
        if (!activeData.active || !activeData.id) {
            appState.config = null;
            renderNoActiveFermentation();
            return;
        }
        
        // 2. Carrega ESTADO COMPLETO em uma única requisição
        const completeState = await apiRequest(`state/complete&config_id=${activeData.id}`);
        
        // 3. Atualiza estado local
        appState.config = completeState.config;
        appState.espState = completeState.state || {};
        appState.readings = completeState.readings || [];
        appState.ispindel = completeState.ispindel;
        appState.controller = completeState.controller;
        appState.lastUpdate = completeState.timestamp;
        appState.heartbeat = completeState.heartbeat;
        
        console.log('✅ Estado completo carregado:', completeState);
        
        // 4. Verifica status do ESP
        checkESPStatus();
        
        // 5. Renderiza UI
        renderUI();
        
    } catch (error) {
        console.error('Erro ao carregar estado:', error);
        renderNoActiveFermentation();
    }
}

// ========== ATUALIZAÇÃO AUTOMÁTICA ==========
async function autoRefreshData() {
    console.log('🔄 Auto-refresh:', new Date().toLocaleTimeString());
    
    try {
        await loadCompleteState();
        console.log('✅ Dados atualizados');
    } catch (error) {
        console.error('❌ Erro no auto-refresh:', error);
    }
}

// ========== FUNÇÕES DE RENDERIZAÇÃO ==========
function renderUI() {
    if (!appState.config) {
        renderNoActiveFermentation();
        return;
    }

    const nameElement = document.getElementById('fermentation-name');
    const stageElement = document.getElementById('stage-info');
    
    if (nameElement) nameElement.textContent = appState.config.name;
    
    if (stageElement) {
        const currentStage = appState.config.current_stage_index + 1;
        const totalStages = appState.config.stages.length;
        stageElement.textContent = `Etapa ${currentStage} de ${totalStages}`;
    }
    
    // Exibe tempo restante (se enviado pelo ESP)
    const timeElement = document.getElementById('time-remaining');
    if (timeElement && appState.espState.timeRemaining) {
        const tr = appState.espState.timeRemaining;
        
        let icon = 'fas fa-clock';
        let statusClass = '';
        
        if (tr.status === 'waiting') {
            icon = 'fas fa-hourglass-start';
            statusClass = 'text-yellow-600';
        } else if (tr.status === 'running') {
            icon = 'fas fa-hourglass-half';
            statusClass = 'text-green-600';
        }
        
        const label = tr.status === 'waiting' ? '(aguardando alvo)' : 'restantes';
        
        timeElement.innerHTML = `
            <i class="${icon} ${statusClass}"></i> 
            <span class="${statusClass}">
                ${tr.value} ${tr.unit} ${label}
            </span>
        `;
        timeElement.style.display = 'flex';
    } else if (timeElement) {
        timeElement.style.display = 'none';
    }
    
    checkESPStatus();
    renderInfoCards();
    renderChart();
    renderStagesList();
}

function renderInfoCards() {
    const infoCards = document.getElementById('info-cards');
    if (!infoCards) return;
    
    // Usa dados do estado ESP ou última leitura
    const lastReading = appState.readings[appState.readings.length - 1] || {};
    const currentStage = appState.config.stages[appState.config.current_stage_index] || {};
    
    const currentTemp = parseFloat(lastReading.temp_fermenter) || 0;
    const fridgeTemp = parseFloat(lastReading.temp_fridge) || 0;
    const targetTemp = parseFloat(lastReading.temp_target) || parseFloat(currentStage.target_temp) || 0;
    
    let tempStatus = '';
    let tempColor = '#9ca3af';
    
    if (currentTemp > 0 && targetTemp > 0) {
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

    const gravityValue = parseFloat(appState.ispindel?.gravity) || 0;
    const batteryInfo = appState.ispindel?.battery ? 
        `Bateria: ${parseFloat(appState.ispindel.battery).toFixed(1)}V` : '';

    const targetGravity = (currentStage.type === 'gravity' || currentStage.type === 'gravity_time') 
        ? parseFloat(currentStage.target_gravity) || 0 
        : 0;
    
    let gravityTargetSubtitle = '';
    if (targetGravity > 0 && gravityValue > 0) {
        const diff = gravityValue - targetGravity;
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

    // Status da contagem (se enviado pelo ESP)
    const countingStatus = appState.espState.targetReached ? 
        '✅ Contagem iniciada' : '⏳ Aguardando alvo';
    const countingColor = appState.espState.targetReached ? '#10b981' : '#f59e0b';

    infoCards.innerHTML = `
        ${cardTemplate({
            title: 'Temp. Fermentador',
            icon: 'fas fa-thermometer-full',
            value: currentTemp > 0 ? `${currentTemp.toFixed(1)}°C` : '--',
            subtitle: tempStatus,
            color: tempColor
        })}
        ${cardTemplate({
            title: 'Temp. Alvo Atual',
            icon: 'fas fa-crosshairs',
            value: targetTemp > 0 ? `${targetTemp.toFixed(1)}°C` : '--',
            subtitle: countingStatus,
            color: countingColor
        })}
        ${cardTemplate({
            title: 'Gravidade Atual',
            icon: 'fas fa-tint',
            value: gravityValue > 0 ? gravityValue.toFixed(3) : '--',
            subtitle: batteryInfo,
            color: '#10b981'
        })}
        ${cardTemplate({
            title: 'Gravidade Alvo',
            icon: 'fas fa-bullseye',
            value: targetGravity > 0 ? targetGravity.toFixed(3) : '--',
            subtitle: gravityTargetSubtitle,
            color: targetGravity > 0 ? '#8b5cf6' : '#9ca3af'
        })}
    `;
}

function renderStagesList() {
    const stagesList = document.getElementById('stages-list');
    if (!stagesList || !appState.config) return;
    
    const currentIndex = appState.config.current_stage_index;
    
    stagesList.innerHTML = appState.config.stages
        .map((stage, index) => {
            const isCurrent = index === currentIndex;
            const isCompleted = stage.status === 'completed';
            
            return stageTemplate(stage, index, isCurrent, isCompleted);
        })
        .join('');
}

function renderChart() {
    const canvas = document.getElementById('fermentation-chart');
    const ctx = canvas ? canvas.getContext('2d') : null;
    const noDataMsg = document.getElementById('no-data-message');

    if (!ctx || appState.readings.length === 0) {
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
        return `${hour}:${minute} ${day}/${month}`;
    });

    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Temp. Geladeira',
                    data: appState.readings.map(r => parseFloat(r.temp_fridge)),
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Fermentador',
                    data: appState.readings.map(r => parseFloat(r.temp_fermenter)),
                    borderColor: '#1e40af',
                    backgroundColor: 'rgba(30, 64, 175, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Alvo',
                    data: appState.readings.map(r => parseFloat(r.temp_target)),
                    borderColor: '#ef4444',
                    borderDash: [5, 5],
                    backgroundColor: 'rgba(239, 68, 68, 0.1)',
                    tension: 0,
                    fill: false,
                    pointRadius: 0
                },
                {
                    label: 'Gravidade (x1000)',
                    data: appState.readings.map(r => r.gravity ? parseFloat(r.gravity) * 1000 : null),
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
            }
        }
    });
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

// ========== TEMPLATES HTML ==========
const cardTemplate = ({ title, icon, value, subtitle, color }) => `
    <div class="card">
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
    switch(stage.type) {
        case 'temperature':
            return `${stage.target_temp}°C por ${stage.duration} dias`;
        case 'gravity':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG`;
        case 'gravity_time':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG (máx ${stage.max_duration} dias)`;
        case 'ramp':
            const direction = stage.direction === 'up' ? '▲' : '▼';
            const rampTimeDisplay = stage.ramp_time < 24 
                ? `${stage.ramp_time} horas` 
                : `${(stage.ramp_time / 24).toFixed(1)} dias`;
            return `${direction} ${stage.start_temp}°C → ${stage.target_temp}°C em ${rampTimeDisplay}`;
        default:
            return '';
    }
}

// ========== INICIALIZAÇÃO ==========
async function initAppAfterAuth() {
    console.log('Inicializando app após autenticação');
    
    try {
        await loadCompleteState();
        
        if (refreshInterval) {
            clearInterval(refreshInterval);
        }
        
        refreshInterval = setInterval(autoRefreshData, REFRESH_INTERVAL);
        
        console.log(`✅ App inicializado - Auto-refresh ativo (${REFRESH_INTERVAL/1000}s)`);
        
    } catch (error) {
        console.error('Erro na inicialização:', error);
        alert('Erro ao inicializar o monitor. Por favor, recarregue a página.');
    }
}

// ========== EXPORTAÇÃO PARA ESCOPO GLOBAL ==========
window.login = login;
window.logout = logout;
window.refreshData = loadCompleteState;

// ========== INICIALIZAÇÃO ==========
document.addEventListener('DOMContentLoaded', async () => {
    console.log('App.js carregado. Verificando autenticação...');
    
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
        }
        return;
    }
    
    // FALLBACK: Verifica última leitura ou última atualização do controlador
    let lastTimestamp = null;
    
    if (appState.readings && appState.readings.length > 0) {
        const lastReading = appState.readings[appState.readings.length - 1];
        lastTimestamp = lastReading.reading_timestamp;
    } else if (appState.controller && appState.controller.state_timestamp) {
        lastTimestamp = appState.controller.state_timestamp;
    }
    
    if (!lastTimestamp) {
        // Sem dados ainda - mostra offline
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = 'sem dados';
        
        // CORREÇÃO: Usar função que já formata corretamente
        const nowUTC = new Date().toISOString(); // Pega UTC atual
        updateLastUpdateText(nowUTC); // formatTimestamp converterá para São Paulo
        
        return;
    }
    
    const diffMs = calculateTimeDifference(lastTimestamp);
    
    // Atualiza texto de última atualização
    updateLastUpdateText(lastTimestamp);
    
    if (diffMs > ESP_OFFLINE_THRESHOLD) {
        // ESP OFFLINE
        alertDiv.classList.remove('hidden');
        badgeDiv.classList.add('hidden');
        if (offlineTimeSpan) offlineTimeSpan.textContent = formatTimeDifference(diffMs);
    } else {
        // ESP ONLINE
        alertDiv.classList.add('hidden');
        badgeDiv.classList.remove('hidden');
    }
}

// ========== CARREGAMENTO DE DADOS (ENDPOINT UNIFICADO) ==========
async function loadCompleteState() {
    try {
        // 1. Verifica se há fermentação ativa
        const activeData = await apiRequest('active');
        
        if (!activeData.active || !activeData.id) {
            appState.config = null;
            renderNoActiveFermentation();
            return;
        }
        
        // 2. Carrega ESTADO COMPLETO em uma única requisição
        const completeState = await apiRequest(`state/complete&config_id=${activeData.id}`);
        
        // 3. Atualiza estado local
        appState.config = completeState.config;
        appState.espState = completeState.state || {};
        appState.readings = completeState.readings || [];
        appState.ispindel = completeState.ispindel;
        appState.controller = completeState.controller;
        appState.lastUpdate = completeState.timestamp;
        appState.heartbeat = completeState.heartbeat;
        
        console.log('✅ Estado completo carregado:', completeState);
        
        // 4. Verifica status do ESP
        checkESPStatus();
        
        // 5. Renderiza UI
        renderUI();
        
    } catch (error) {
        console.error('Erro ao carregar estado:', error);
        renderNoActiveFermentation();
    }
}

// ========== ATUALIZAÇÃO AUTOMÁTICA ==========
async function autoRefreshData() {
    console.log('🔄 Auto-refresh:', new Date().toLocaleTimeString());
    
    try {
        await loadCompleteState();
        console.log('✅ Dados atualizados');
    } catch (error) {
        console.error('❌ Erro no auto-refresh:', error);
    }
}

// ========== FUNÇÕES DE RENDERIZAÇÃO ==========
function renderUI() {
    if (!appState.config) {
        renderNoActiveFermentation();
        return;
    }

    const nameElement = document.getElementById('fermentation-name');
    const stageElement = document.getElementById('stage-info');
    
    if (nameElement) nameElement.textContent = appState.config.name;
    
    if (stageElement) {
        const currentStage = appState.config.current_stage_index + 1;
        const totalStages = appState.config.stages.length;
        stageElement.textContent = `Etapa ${currentStage} de ${totalStages}`;
    }
    
    // Exibe tempo restante (se enviado pelo ESP)
    const timeElement = document.getElementById('time-remaining');
    if (timeElement && appState.espState.timeRemaining) {
        const tr = appState.espState.timeRemaining;
        
        let icon = 'fas fa-clock';
        let statusClass = '';
        
        if (tr.status === 'waiting') {
            icon = 'fas fa-hourglass-start';
            statusClass = 'text-yellow-600';
        } else if (tr.status === 'running') {
            icon = 'fas fa-hourglass-half';
            statusClass = 'text-green-600';
        }
        
        const label = tr.status === 'waiting' ? '(aguardando alvo)' : 'restantes';
        
        timeElement.innerHTML = `
            <i class="${icon} ${statusClass}"></i> 
            <span class="${statusClass}">
                ${tr.value} ${tr.unit} ${label}
            </span>
        `;
        timeElement.style.display = 'flex';
    } else if (timeElement) {
        timeElement.style.display = 'none';
    }
    
    checkESPStatus();
    renderInfoCards();
    renderChart();
    renderStagesList();
}

function renderInfoCards() {
    const infoCards = document.getElementById('info-cards');
    if (!infoCards) return;
    
    // Usa dados do estado ESP ou última leitura
    const lastReading = appState.readings[appState.readings.length - 1] || {};
    const currentStage = appState.config.stages[appState.config.current_stage_index] || {};
    
    const currentTemp = parseFloat(lastReading.temp_fermenter) || 0;
    const fridgeTemp = parseFloat(lastReading.temp_fridge) || 0;
    const targetTemp = parseFloat(lastReading.temp_target) || parseFloat(currentStage.target_temp) || 0;
    
    let tempStatus = '';
    let tempColor = '#9ca3af';
    
    if (currentTemp > 0 && targetTemp > 0) {
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

    const gravityValue = parseFloat(appState.ispindel?.gravity) || 0;
    const batteryInfo = appState.ispindel?.battery ? 
        `Bateria: ${parseFloat(appState.ispindel.battery).toFixed(1)}V` : '';

    const targetGravity = (currentStage.type === 'gravity' || currentStage.type === 'gravity_time') 
        ? parseFloat(currentStage.target_gravity) || 0 
        : 0;
    
    let gravityTargetSubtitle = '';
    if (targetGravity > 0 && gravityValue > 0) {
        const diff = gravityValue - targetGravity;
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

    // Status da contagem (se enviado pelo ESP)
    const countingStatus = appState.espState.targetReached ? 
        '✅ Contagem iniciada' : '⏳ Aguardando alvo';
    const countingColor = appState.espState.targetReached ? '#10b981' : '#f59e0b';

    infoCards.innerHTML = `
        ${cardTemplate({
            title: 'Temp. Fermentador',
            icon: 'fas fa-thermometer-full',
            value: currentTemp > 0 ? `${currentTemp.toFixed(1)}°C` : '--',
            subtitle: tempStatus,
            color: tempColor
        })}
        ${cardTemplate({
            title: 'Temp. Alvo Atual',
            icon: 'fas fa-crosshairs',
            value: targetTemp > 0 ? `${targetTemp.toFixed(1)}°C` : '--',
            subtitle: countingStatus,
            color: countingColor
        })}
        ${cardTemplate({
            title: 'Gravidade Atual',
            icon: 'fas fa-tint',
            value: gravityValue > 0 ? gravityValue.toFixed(3) : '--',
            subtitle: batteryInfo,
            color: '#10b981'
        })}
        ${cardTemplate({
            title: 'Gravidade Alvo',
            icon: 'fas fa-bullseye',
            value: targetGravity > 0 ? targetGravity.toFixed(3) : '--',
            subtitle: gravityTargetSubtitle,
            color: targetGravity > 0 ? '#8b5cf6' : '#9ca3af'
        })}
    `;
}

function renderStagesList() {
    const stagesList = document.getElementById('stages-list');
    if (!stagesList || !appState.config) return;
    
    const currentIndex = appState.config.current_stage_index;
    
    stagesList.innerHTML = appState.config.stages
        .map((stage, index) => {
            const isCurrent = index === currentIndex;
            const isCompleted = stage.status === 'completed';
            
            return stageTemplate(stage, index, isCurrent, isCompleted);
        })
        .join('');
}

function renderChart() {
    const canvas = document.getElementById('fermentation-chart');
    const ctx = canvas ? canvas.getContext('2d') : null;
    const noDataMsg = document.getElementById('no-data-message');

    if (!ctx || appState.readings.length === 0) {
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
        return date.toLocaleTimeString('pt-BR', { 
            timeZone: SAO_PAULO_TIMEZONE,
            hour: '2-digit', 
            minute: '2-digit',
            day: '2-digit',
            month: '2-digit'
        });
    });

    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Temp. Geladeira',
                    data: appState.readings.map(r => parseFloat(r.temp_fridge)),
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Fermentador',
                    data: appState.readings.map(r => parseFloat(r.temp_fermenter)),
                    borderColor: '#1e40af',
                    backgroundColor: 'rgba(30, 64, 175, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Alvo',
                    data: appState.readings.map(r => parseFloat(r.temp_target)),
                    borderColor: '#ef4444',
                    borderDash: [5, 5],
                    backgroundColor: 'rgba(239, 68, 68, 0.1)',
                    tension: 0,
                    fill: false,
                    pointRadius: 0
                },
                {
                    label: 'Gravidade (x1000)',
                    data: appState.readings.map(r => r.gravity ? parseFloat(r.gravity) * 1000 : null),
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
            }
        }
    });
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

// ========== TEMPLATES HTML ==========
const cardTemplate = ({ title, icon, value, subtitle, color }) => `
    <div class="card">
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
    switch(stage.type) {
        case 'temperature':
            return `${stage.target_temp}°C por ${stage.duration} dias`;
        case 'gravity':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG`;
        case 'gravity_time':
            return `${stage.target_temp}°C até ${stage.target_gravity} SG (máx ${stage.max_duration} dias)`;
        case 'ramp':
            const direction = stage.direction === 'up' ? '▲' : '▼';
            const rampTimeDisplay = stage.ramp_time < 24 
                ? `${stage.ramp_time} horas` 
                : `${(stage.ramp_time / 24).toFixed(1)} dias`;
            return `${direction} ${stage.start_temp}°C → ${stage.target_temp}°C em ${rampTimeDisplay}`;
        default:
            return '';
    }
}

// ========== INICIALIZAÇÃO ==========
async function initAppAfterAuth() {
    console.log('Inicializando app após autenticação');
    
    try {
        await loadCompleteState();
        
        if (refreshInterval) {
            clearInterval(refreshInterval);
        }
        
        refreshInterval = setInterval(autoRefreshData, REFRESH_INTERVAL);
        
        console.log(`✅ App inicializado - Auto-refresh ativo (${REFRESH_INTERVAL/1000}s)`);
        
    } catch (error) {
        console.error('Erro na inicialização:', error);
        alert('Erro ao inicializar o monitor. Por favor, recarregue a página.');
    }
}

// ========== EXPORTAÇÃO PARA ESCOPO GLOBAL ==========
window.login = login;
window.logout = logout;
window.refreshData = loadCompleteState;

// ========== INICIALIZAÇÃO ==========
document.addEventListener('DOMContentLoaded', async () => {
    console.log('App.js carregado. Verificando autenticação...');
    
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