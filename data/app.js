// app.js - Monitor de Fermentação com MySQL
const API_BASE_URL = '/api.php?path='; // Ajuste para o caminho correto da sua API

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;

// ========== ESTADO DA APLICAÇÃO ==========
let state = {
    config: null,
    readings: [],
    latestReading: null,
    controllerState: null,
    fermentationState: null,
    stageTimers: {}
};

// ========== CONSTANTES ==========
const TEMP_TOLERANCE = 0.5; // ±0.5°C de tolerância
const CHECK_INTERVAL = 5 * 60 * 1000; // Verificar a cada 5 minutos

// ========== FUNÇÕES DE API ==========
async function apiRequest(endpoint, options = {}) {
    try {
        const response = await fetch(API_BASE_URL + endpoint, {
            ...options,
            headers: {
                'Content-Type': 'application/json',
                ...options.headers
            },
            credentials: 'same-origin' // Importante para manter sessão
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
        
        // Limpar estado
        state.config = null;
        state.readings = [];
        state.latestReading = null;
        state.stageTimers = {};
        localStorage.removeItem('stageTimers');
        
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

// ========== FUNÇÕES DE DADOS ==========
async function loadActiveConfig() {
    try {
        const activeData = await apiRequest('active');
        
        if (activeData.active && activeData.id) {
            await loadConfigById(activeData.id);
            await loadReadings(activeData.id);
            await loadControllerState();
            await loadFermentationState(activeData.id);
        } else {
            state.config = null;
            renderNoActiveFermentation();
        }
    } catch (error) {
        console.error('Erro ao carregar configuração ativa:', error);
        renderNoActiveFermentation();
    }
}

async function loadConfigById(configId) {
    try {
        const configs = await apiRequest('configurations');
        const config = configs.find(c => c.id == configId);
        
        if (config) {
            state.config = config;
            console.log('Configuração carregada:', state.config.name);
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

async function loadReadings(configId) {
    if (!configId) return;
    
    try {
        const readings = await apiRequest(`readings&config_id=${configId}`);
        
        state.readings = readings.map(r => ({
            ...r,
            timestamp: new Date(r.reading_timestamp)
        })).sort((a, b) => a.timestamp - b.timestamp);
        
        if (state.readings.length > 0) {
            const newLatest = state.readings[state.readings.length - 1];
            
            if (!state.latestReading || newLatest.id !== state.latestReading.id) {
                state.latestReading = newLatest;
                updateStageStatus();
            }
            renderUI();
        }
    } catch (error) {
        console.error('Erro ao carregar leituras:', error);
    }
}

async function loadControllerState() {
    try {
        state.controllerState = await apiRequest('control');
        renderControllerStatus();
    } catch (error) {
        console.error('Erro ao carregar estado do controlador:', error);
    }
}

async function loadFermentationState(configId) {
    try {
        const stateData = await apiRequest(`fermentation-state&config_id=${configId}`);
        state.fermentationState = stateData;
        
        if (stateData.stage_timers) {
            state.stageTimers = stateData.stage_timers;
        }
        
        renderUI();
    } catch (error) {
        console.error('Erro ao carregar estado da fermentação:', error);
    }
}

// ========== FUNÇÕES DE CONTROLE DE TEMPERATURA ==========
function isTemperatureOnTarget(currentTemp, targetTemp) {
    if (!currentTemp || !targetTemp) return false;
    return Math.abs(currentTemp - targetTemp) <= TEMP_TOLERANCE;
}

function updateStageStatus() {
    if (!state.config || !state.latestReading) return;
    
    const currentStageIndex = state.config.current_stage_index;
    const currentStage = state.config.stages[currentStageIndex];
    
    if (!currentStage) return;
    
    const currentTemp = state.latestReading.temp_fermenter;
    let targetTemp = currentStage.target_temp;
    
    // Para rampas, calcular temperatura alvo atual
    if (currentStage.type === 'ramp') {
        targetTemp = calculateCurrentTargetTemp(currentStage) || targetTemp;
    }
    
    const stageKey = `${state.config.id}_${currentStageIndex}`;
    
    // Se JÁ ATINGIU O ALVO UMA VEZ, NÃO VERIFICA MAIS
    if (state.stageTimers[stageKey]?.targetReachedTime) {
        console.log(`⏱️ Etapa ${currentStageIndex + 1} já atingiu alvo. Contagem contínua.`);
        return;
    }
    
    // Verificar se atingiu a temperatura alvo pela PRIMEIRA VEZ
    if (isTemperatureOnTarget(currentTemp, targetTemp)) {
        if (!state.stageTimers[stageKey]) {
            state.stageTimers[stageKey] = {};
        }
        
        state.stageTimers[stageKey].targetReachedTime = new Date().toISOString();
        
        console.log(`🎯 PRIMEIRA VEZ! Etapa ${currentStageIndex + 1} atingiu temperatura alvo!`);
        console.log(`⏱️ Iniciando contagem PERMANENTE às ${new Date().toLocaleTimeString()}`);
        
        saveStageTimers();
        renderUI();
    }
}

function calculateCurrentTargetTemp(stage) {
    if (!stage) return null;
    
    if (stage.type === 'ramp' && stage.start_time) {
        const progress = calculateRampProgress(stage);
        if (progress !== null) {
            const tempDiff = stage.target_temp - stage.start_temp;
            return stage.start_temp + (tempDiff * progress);
        }
    }
    
    return stage.target_temp;
}

function calculateRampProgress(stage) {
    if (!stage || stage.type !== 'ramp' || !stage.start_time) {
        return null;
    }
    
    const startTime = new Date(stage.start_time);
    const now = new Date();
    const elapsedMs = now - startTime;
    const elapsedHours = elapsedMs / (1000 * 60 * 60);
    
    if (elapsedHours <= 0) return 0;
    if (elapsedHours >= stage.ramp_time) return 1;
    
    return elapsedHours / stage.ramp_time;
}

function saveStageTimers() {
    try {
        localStorage.setItem('stageTimers', JSON.stringify(state.stageTimers));
        console.log('💾 Temporizadores salvos:', state.stageTimers);
    } catch (error) {
        console.error('Erro ao salvar temporizadores:', error);
    }
}

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
            `Etapa ${state.config.current_stage_index + 1} de ${state.config.stages.length}`;
    }
    
    // Tempo restante
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

    // Renderizar cards
    renderInfoCards();
    
    // Renderizar gráfico
    renderChart();

    // Renderizar lista de etapas
    renderStagesList();
}

function renderInfoCards() {
    const currentStage = getCurrentStage();
    const infoCards = document.getElementById('info-cards');
    
    if (!infoCards) return;
    
    const stageKey = `${state.config.id}_${state.config.current_stage_index}`;
    const targetReached = state.stageTimers[stageKey]?.targetReachedTime;
    const currentTemp = state.latestReading?.temp_fermenter;
    const targetTemp = currentStage ? 
        (currentStage.type === 'ramp' ? 
            calculateCurrentTargetTemp(currentStage) : 
            currentStage.target_temp) : 
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
            value: state.latestReading ? `${state.latestReading.temp_fridge.toFixed(1)}°C` : '--',
            color: '#3b82f6'
        })}
        ${cardTemplate({
            title: 'Temp. Fermentador',
            icon: 'fas fa-thermometer-full',
            value: state.latestReading ? `${state.latestReading.temp_fermenter.toFixed(1)}°C` : '--',
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
            value: targetTemp ? `${targetTemp.toFixed(1)}°C` : '--',
            subtitle: targetReached ? '✅ Contagem iniciada' : '⏳ Aguardando alvo',
            color: targetReached ? '#10b981' : '#f59e0b'
        })}
    `;
}

function renderStagesList() {
    const stagesList = document.getElementById('stages-list');
    if (!stagesList || !state.config) return;
    
    stagesList.innerHTML = state.config.stages
        .map((stage, index) => {
            const stageKey = `${state.config.id}_${index}`;
            const targetReached = state.stageTimers[stageKey]?.targetReachedTime;
            const isCurrent = index === state.config.current_stage_index;
            
            return stageTemplate(stage, index, state.config.current_stage_index, targetReached, isCurrent);
        })
        .join('');
}

function renderControllerStatus() {
    const setpointElement = document.getElementById('current-setpoint');
    const coolingElement = document.getElementById('cooling-status');
    const heatingElement = document.getElementById('heating-status');
    
    if (setpointElement && state.controllerState?.setpoint) {
        setpointElement.textContent = `${state.controllerState.setpoint.toFixed(1)}°C`;
    }
    
    if (coolingElement) {
        coolingElement.textContent = state.controllerState?.cooling ? 'ON' : 'OFF';
        coolingElement.className = state.controllerState?.cooling ? 
            'text-2xl font-bold text-blue-500' : 'text-2xl font-bold text-gray-500';
    }
    
    if (heatingElement) {
        heatingElement.textContent = state.controllerState?.heating ? 'ON' : 'OFF';
        heatingElement.className = state.controllerState?.heating ? 
            'text-2xl font-bold text-red-500' : 'text-2xl font-bold text-gray-500';
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

    if (chart) {
        chart.destroy();
    }

    const labels = state.readings.map(r => 
        r.timestamp.toLocaleTimeString('pt-BR', { 
            hour: '2-digit', 
            minute: '2-digit',
            day: '2-digit',
            month: '2-digit'
        })
    );

    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Temp. Geladeira',
                    data: state.readings.map(r => r.temp_fridge),
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Fermentador',
                    data: state.readings.map(r => r.temp_fermenter),
                    borderColor: '#1e40af',
                    backgroundColor: 'rgba(30, 64, 175, 0.1)',
                    tension: 0.4,
                    fill: false
                },
                {
                    label: 'Temp. Alvo',
                    data: state.readings.map(r => r.temp_target),
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

const stageTemplate = (stage, index, currentIndex, targetReached, isCurrent) => {
    const isRamp = stage.type === 'ramp';
    
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
            </div>
        </div>
    </div>
    `;
};

// ========== FUNÇÕES AUXILIARES ==========
function getCurrentStage() {
    if (!state.config || !state.config.stages) return null;
    return state.config.stages[state.config.current_stage_index];
}

function getStageDescription(stage) {
    if (!stage) return '';
    
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

function getTimeRemaining() {
    const stage = getCurrentStage();
    if (!stage || !stage.start_time) return null;

    const stageKey = `${state.config.id}_${state.config.current_stage_index}`;
    
    if (stage.type === 'ramp') {
        const start = new Date(stage.start_time);
        const now = new Date();
        const elapsedHours = (now - start) / (1000 * 60 * 60);
        const remaining = stage.ramp_time - elapsedHours;
        
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
    
    const targetReachedTime = state.stageTimers[stageKey]?.targetReachedTime;
    
    if (!targetReachedTime) {
        if (stage.type === 'temperature') {
            return {
                value: stage.duration.toFixed(1),
                unit: 'dias',
                status: 'waiting'
            };
        }
        return null;
    }
    
    const start = new Date(targetReachedTime);
    const now = new Date();
    
    if (stage.type === 'temperature') {
        const elapsed = (now - start) / (1000 * 60 * 60 * 24);
        const remaining = stage.duration - elapsed;
        return {
            value: remaining > 0 ? remaining.toFixed(1) : 0,
            unit: 'dias',
            status: 'running'
        };
    }

    return null;
}

// ========== INICIALIZAÇÃO ==========
async function initAppAfterAuth() {
    console.log('Inicializando app após autenticação');
    
    try {
        // Carregar temporizadores do localStorage
        const savedTimers = localStorage.getItem('stageTimers');
        if (savedTimers) {
            state.stageTimers = JSON.parse(savedTimers);
            console.log('⏱️ Temporizadores restaurados:', state.stageTimers);
        }
        
        await loadActiveConfig();
        
        // Configurar polling
        if (refreshInterval) {
            clearInterval(refreshInterval);
        }
        
        refreshInterval = setInterval(() => {
            if (state.config) {
                loadReadings(state.config.id);
                loadControllerState();
                loadFermentationState(state.config.id);
            }
        }, CHECK_INTERVAL);
        
        console.log('App inicializado com sucesso');
        
    } catch (error) {
        console.error('Erro na inicialização:', error);
        alert('Erro ao inicializar o monitor. Por favor, recarregue a página.');
    }
}

// ========== EXPORTAÇÃO PARA ESCOPO GLOBAL ==========
window.login = login;
window.logout = logout;
window.refreshData = loadActiveConfig;

// ========== INICIALIZAÇÃO ==========
document.addEventListener('DOMContentLoaded', async () => {
    console.log('App.js carregado. Verificando autenticação...');
    
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
    
    await checkAuthStatus();
});