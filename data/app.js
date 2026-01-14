// app.js - Monitor Passivo (COM visualização de Cooler/Heater)
const API_BASE_URL = '/api.php?path=';

// ========== VARIÁVEIS GLOBAIS ==========
let chart = null;
let refreshInterval = null;
let isAppInitialized = false;

const REFRESH_INTERVAL = 30000;
const ESP_OFFLINE_THRESHOLD = 120000;
const SAO_PAULO_UTC_OFFSET = -3 * 60 * 60 * 1000;

// ========== ESTADO DA APLICAÇÃO ==========
let appState = {
    config: null,
    espState: null,
    readings: [],
    ispindel: null,
    controller: null,
    controllerHistory: [],  // ✅ NOVO: Histórico de estados
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
            controller: null,
            controllerHistory: [],
            heartbeat: null,
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
    if (!tr || !tr.value || !tr.unit) {
        return '--';
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
    else if (tr.unit === 'indefinite') {
        return 'Aguardando gravidade';
    }
    
    return `${tr.value} ${tr.unit}`;
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

function updateRelayStatus() {
    const coolerStatusDiv = document.getElementById('cooler-status');
    const heaterStatusDiv = document.getElementById('heater-status');
    const relayContainer = document.getElementById('relay-status');
    
    if (!coolerStatusDiv || !heaterStatusDiv || !relayContainer) return;
    
    let coolerActive = false;
    let heaterActive = false;
    let waitingStatus = null;
    
    if (appState.heartbeat && appState.heartbeat.control_status) {
        const cs = appState.heartbeat.control_status;
        
        coolerActive = appState.heartbeat.cooler_active === 1 || appState.heartbeat.cooler_active === true;
        heaterActive = appState.heartbeat.heater_active === 1 || appState.heartbeat.heater_active === true;
        
        if (cs.is_waiting && cs.wait_reason) {
            waitingStatus = {
                reason: cs.wait_reason,
                display: cs.wait_display || 'aguardando'
            };
        }
    } else if (appState.controller) {
        coolerActive = appState.controller.cooling === 1 || appState.controller.cooling === true;
        heaterActive = appState.controller.heating === 1 || appState.controller.heating === true;
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

// ========== CARREGAMENTO ==========
async function loadCompleteState() {
    try {
        const activeData = await apiRequest('active');
        
        if (!activeData.active || !activeData.id) {
            appState.config = null;
            renderNoActiveFermentation();
            return;
        }
        
        const completeState = await apiRequest(`state/complete&config_id=${activeData.id}`);
        
        appState.config = completeState.config;
        appState.espState = completeState.state || {};
        appState.readings = completeState.readings || [];
        appState.ispindel = completeState.ispindel;
        appState.controller = completeState.controller;
        appState.controllerHistory = completeState.controller_history || [];  // ✅ NOVO!
        appState.lastUpdate = completeState.timestamp;
        appState.heartbeat = completeState.heartbeat;
        
        console.log('✅ Estado completo carregado:', completeState);
        console.log('📊 Histórico controlador:', appState.controllerHistory.length, 'registros');
        
        checkESPStatus();
        renderUI();
        
    } catch (error) {
        console.error('Erro ao carregar estado:', error);
        renderNoActiveFermentation();
    }
}

async function autoRefreshData() {
    console.log('🔄 Auto-refresh:', new Date().toLocaleTimeString());
    
    try {
        await loadCompleteState();
        console.log('✅ Dados atualizados');
    } catch (error) {
        console.error('❌ Erro no auto-refresh:', error);
    }
}

// ========== RENDERIZAÇÃO ==========
function renderUI() {
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
        stageElement.textContent = `Etapa ${currentStage} de ${totalStages}`;
    }
    
    const timeElement = document.getElementById('time-remaining');
    if (timeElement && appState.espState && appState.espState.timeRemaining) {
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
        const timeDisplay = formatTimeRemaining(tr);
        
        timeElement.innerHTML = `
            <i class="${icon} ${statusClass}"></i> 
            <span class="${statusClass}">
                ${timeDisplay} ${label}
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

    const isReallyRunning = appState.espState.targetReached && 
                            appState.espState.timeRemaining?.status === 'running';
    
    const countingStatus = isReallyRunning 
        ? '✅ Contagem iniciada' 
        : '⏳ Aguardando alvo';
    
    const countingColor = isReallyRunning ? '#10b981' : '#f59e0b';

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
        
        infoCards.innerHTML += cardTemplate({
            title: 'Status do Controle',
            icon: statusIcon,
            value: statusText,
            subtitle: statusSubtitle,
            color: statusColor
        });
    }
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

// ========== GRÁFICO COM VISUALIZAÇÃO DE COOLER/HEATER ==========
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
        return `${hour}:${minute} ${day}/${month}`;
    });

    // ✅ Processa histórico de controller_states
    const coolerData = [];
    const heaterData = [];
    
    if (appState.controllerHistory && appState.controllerHistory.length > 0) {
        appState.readings.forEach((reading) => {
            const readingTime = new Date(reading.reading_timestamp).getTime();
            
            // Busca estado do controlador mais próximo (dentro de 5 minutos)
            const controllerState = appState.controllerHistory.find(cs => {
                const stateTime = new Date(cs.state_timestamp).getTime();
                return Math.abs(stateTime - readingTime) < 300000;
            });
            
            if (controllerState) {
                const coolerActive = controllerState.cooling === 1 || controllerState.cooling === true;
                const heaterActive = controllerState.heating === 1 || controllerState.heating === true;
                
                // Mostra temperatura da geladeira quando relé ativo
                coolerData.push(coolerActive ? parseFloat(reading.temp_fridge) : null);
                heaterData.push(heaterActive ? parseFloat(reading.temp_fridge) : null);
            } else {
                coolerData.push(null);
                heaterData.push(null);
            }
        });
    }

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
            label: 'Gravidade (x1000)',
            data: appState.readings.map(r => r.gravity ? parseFloat(r.gravity) * 1000 : null),
            borderColor: '#10b981',
            backgroundColor: 'rgba(16, 185, 129, 0.1)',
            tension: 0.4,
            fill: false,
            yAxisID: 'y1',
            order: 2
        }
    ];

    // ✅ Adiciona datasets de cooler/heater (aparecem como áreas preenchidas)
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
                        font: {
                            size: 11
                        }
                    }
                },
                tooltip: {
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            
                            if (label.includes('Cooler Ativo') || label.includes('Heater Ativo')) {
                                return context.parsed.y !== null ? label : null;
                            }
                            
                            if (label) {
                                label += ': ';
                            }
                            if (context.parsed.y !== null) {
                                if (context.dataset.yAxisID === 'y1') {
                                    label += (context.parsed.y / 1000).toFixed(3);
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

// ========== TEMPLATES ==========
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

window.login = login;
window.logout = logout;
window.refreshData = loadCompleteState;

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