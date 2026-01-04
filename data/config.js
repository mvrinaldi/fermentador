// config.js - Sistema de Configuração com MySQL
const API_BASE_URL = '/api.php?path=';

// ========== VARIÁVEIS GLOBAIS ==========
let stages = [];
let savedConfigs = [];
let currentRampData = null;

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
        alert('Por favor, preencha email e senha');
        return;
    }
    
    try {
        await apiRequest('auth/login', {
            method: 'POST',
            body: JSON.stringify({ email, password })
        });
        
        hideLoginScreen();
        await loadConfigurations();
    } catch (error) {
        alert('Erro no login: ' + error.message);
    }
}

async function logout() {
    if (confirm('Deseja realmente sair?')) {
        try {
            await apiRequest('auth/logout', { method: 'POST' });
            window.location.href = 'index.html';
        } catch (error) {
            console.error('Erro ao fazer logout:', error);
            alert('Erro ao fazer logout: ' + error.message);
        }
    }
}

async function checkAuthStatus() {
    try {
        const result = await apiRequest('auth/check');
        
        if (result.authenticated) {
            hideLoginScreen();
            await loadConfigurations();
        } else {
            showLoginScreen();
        }
    } catch (error) {
        console.error('Erro ao verificar autenticação:', error);
        showLoginScreen();
    }
}

function showLoginScreen() {
    const loginScreen = document.getElementById('login-screen');
    const container = document.querySelector('.container');
    if (loginScreen) loginScreen.style.display = 'block';
    if (container) container.style.display = 'none';
}

function hideLoginScreen() {
    const loginScreen = document.getElementById('login-screen');
    const container = document.querySelector('.container');
    if (loginScreen) loginScreen.style.display = 'none';
    if (container) container.style.display = 'block';
}

// ========== FUNÇÕES DE RAMPA ==========
function addStageWithRamp() {
    if (stages.length === 0) {
        addStage();
    } else {
        openRampModal();
    }
}

function openRampModal() {
    if (stages.length === 0) return;
    
    const lastStage = stages[stages.length - 1];
    const modal = document.getElementById('ramp-modal');
    const startTempInput = document.getElementById('ramp-start-temp');
    const prevTempDisplay = document.getElementById('prev-temp-display');
    const rampTargetTemp = document.getElementById('ramp-target-temp');
    
    currentRampData = {
        previousStageId: lastStage.id,
        previousTemp: lastStage.targetTemp,
        rampTime: 24,
        targetTemp: lastStage.targetTemp + 2,
        maxRampRate: 2
    };
    
    startTempInput.value = lastStage.targetTemp;
    prevTempDisplay.textContent = `${lastStage.targetTemp}°C`;
    rampTargetTemp.value = currentRampData.targetTemp;
    document.getElementById('ramp-time').value = currentRampData.rampTime;
    document.getElementById('max-ramp-rate').value = currentRampData.maxRampRate;
    
    updateRampVisualization();
    modal.style.display = 'flex';
}

function closeRampModal() {
    const modal = document.getElementById('ramp-modal');
    modal.style.display = 'none';
    currentRampData = null;
}

function updateRampVisualization() {
    if (!currentRampData) return;
    
    const rampTime = parseFloat(document.getElementById('ramp-time').value) || 24;
    const maxRampRate = parseFloat(document.getElementById('max-ramp-rate').value) || 2;
    const targetTemp = parseFloat(document.getElementById('ramp-target-temp').value) || currentRampData.previousTemp;
    
    currentRampData.rampTime = rampTime;
    currentRampData.maxRampRate = maxRampRate;
    currentRampData.targetTemp = targetTemp;
    
    const tempDifference = Math.abs(targetTemp - currentRampData.previousTemp);
    const rampTimeInDays = rampTime / 24;
    const actualRampRate = tempDifference / rampTimeInDays;
    
    document.getElementById('next-temp-display').textContent = `${targetTemp}°C`;
    document.getElementById('ramp-time-display').textContent = `${rampTime} hora${rampTime !== 1 ? 's' : ''}`;
    document.getElementById('ramp-rate-display').textContent = `${actualRampRate.toFixed(1)}°C/dia`;
    
    const rampLine = document.getElementById('ramp-line');
    const warningDiv = document.getElementById('ramp-warning');
    const warningText = document.getElementById('ramp-warning-text');
    
    if (actualRampRate > maxRampRate) {
        const requiredDays = (tempDifference / maxRampRate).toFixed(1);
        const requiredHours = (requiredDays * 24).toFixed(0);
        warningText.textContent = `Atenção: Taxa de ${actualRampRate.toFixed(1)}°C/dia excede o limite de ${maxRampRate}°C/dia. Recomendado: ${requiredHours} horas.`;
        warningDiv.classList.remove('hidden');
        rampLine.style.background = 'linear-gradient(90deg, #3b82f6, #ef4444)';
    } else {
        warningDiv.classList.add('hidden');
        const gradient = targetTemp > currentRampData.previousTemp 
            ? 'linear-gradient(90deg, #3b82f6, #ef4444)'
            : 'linear-gradient(90deg, #3b82f6, #0ea5e9)';
        rampLine.style.background = gradient;
    }
}

function saveRampConfiguration() {
    if (!currentRampData) {
        closeRampModal();
        return;
    }
    
    // Criar etapa de rampa
    const rampStage = {
        id: Date.now(),
        type: 'ramp',
        startTemp: currentRampData.previousTemp,
        targetTemp: currentRampData.targetTemp,
        rampTime: currentRampData.rampTime,
        maxRampRate: currentRampData.maxRampRate,
        actualRate: Math.abs(currentRampData.targetTemp - currentRampData.previousTemp) / (currentRampData.rampTime / 24),
        direction: currentRampData.targetTemp > currentRampData.previousTemp ? 'up' : 'down'
    };
    
    stages.push(rampStage);
    
    // Criar etapa principal após a rampa
    const mainStage = {
        id: Date.now() + 1,
        type: 'temperature',
        targetTemp: currentRampData.targetTemp,
        duration: 7,
        rampTime: 0
    };
    
    stages.push(mainStage);
    
    closeRampModal();
    renderStages();
}

// ========== FUNÇÕES DE GERENCIAMENTO DE ETAPAS ==========
function addStage() {
    const newStage = {
        id: Date.now(),
        type: 'temperature',
        targetTemp: 18,
        duration: 7,
        targetGravity: 1.010,
        maxDuration: 14,
        rampTime: 0
    };
    stages.push(newStage);
    renderStages();
}

function removeStage(id) {
    stages = stages.filter(stage => stage.id !== id);
    renderStages();
}

function updateStage(id, field, value) {
    stages = stages.map(stage => {
        if (stage.id === id) {
            const updatedStage = { ...stage, [field]: value };
            
            if (stage.type === 'ramp' && (field === 'startTemp' || field === 'targetTemp' || field === 'rampTime')) {
                const tempDiff = Math.abs(updatedStage.targetTemp - updatedStage.startTemp);
                const rampTimeInDays = updatedStage.rampTime / 24;
                updatedStage.actualRate = tempDiff / rampTimeInDays;
                updatedStage.direction = updatedStage.targetTemp > updatedStage.startTemp ? 'up' : 'down';
            }
            
            return updatedStage;
        }
        return stage;
    });
    renderStages();
}

function moveStage(index, direction) {
    const newIndex = direction === 'up' ? index - 1 : index + 1;
    
    if (newIndex >= 0 && newIndex < stages.length) {
        [stages[index], stages[newIndex]] = [stages[newIndex], stages[index]];
        renderStages();
    }
}

// ========== FUNÇÕES DE VALIDAÇÃO ==========
function validateConfiguration() {
    const fermentationName = document.getElementById('fermentation-name');
    if (!fermentationName) return false;
    
    const nameValue = fermentationName.value.trim();
    
    if (!nameValue) {
        alert('Por favor, insira um nome para a fermentação');
        return false;
    }
    
    if (stages.length === 0) {
        alert('Adicione pelo menos uma etapa');
        return false;
    }

    let hasTemperatureStage = false;
    for (let i = 0; i < stages.length; i++) {
        const stage = stages[i];
        
        if (stage.type === 'ramp') {
            if (stage.rampTime <= 0) {
                alert(`Rampa ${i + 1}: Tempo da rampa deve ser maior que 0 horas`);
                return false;
            }
            if (stage.startTemp === stage.targetTemp) {
                alert(`Rampa ${i + 1}: Temperatura inicial e final não podem ser iguais`);
                return false;
            }
        } else if (stage.type === 'temperature') {
            hasTemperatureStage = true;
            if (!stage.duration || stage.duration <= 0) {
                alert(`Etapa ${i + 1}: Duração deve ser maior que 0 dias`);
                return false;
            }
        } else if (stage.type === 'gravity_time') {
            hasTemperatureStage = true;
            if (!stage.maxDuration || stage.maxDuration <= 0) {
                alert(`Etapa ${i + 1}: Duração máxima deve ser maior que 0 dias`);
                return false;
            }
        }
    }

    if (!hasTemperatureStage) {
        alert('Adicione pelo menos uma etapa de temperatura');
        return false;
    }

    return true;
}

// ========== FUNÇÕES DE CONFIGURAÇÃO ==========
async function loadConfigurations() {
    try {
        savedConfigs = await apiRequest('configurations');
        renderSavedConfigs();
    } catch (error) {
        console.error('Erro ao carregar configurações:', error);
        alert('Erro ao carregar configurações: ' + error.message);
    }
}

async function saveConfiguration() {
    if (!validateConfiguration()) return;

    const fermentationName = document.getElementById('fermentation-name').value;
    
    const configData = {
        name: fermentationName,
        stages: stages.map((stage, index) => ({
            type: stage.type,
            targetTemp: stage.targetTemp,
            duration: stage.duration,
            targetGravity: stage.targetGravity,
            maxDuration: stage.maxDuration,
            startTemp: stage.startTemp,
            rampTime: stage.rampTime,
            maxRampRate: stage.maxRampRate,
            actualRate: stage.actualRate,
            direction: stage.direction
        }))
    };

    try {
        await apiRequest('configurations', {
            method: 'POST',
            body: JSON.stringify(configData)
        });
        
        alert('Configuração salva com sucesso!');
        await loadConfigurations();
        document.getElementById('fermentation-name').value = '';
        stages = [];
        renderStages();
    } catch (error) {
        console.error('Erro ao salvar:', error);
        alert('Erro ao salvar configuração: ' + error.message);
    }
}

async function startFermentation(configId, isReusing = false) {
    const activeData = await apiRequest('active');
    if (activeData.active) {
        alert('Já existe uma fermentação ativa! Para iniciar uma nova, primeiro pause ou conclua a fermentação atual.');
        return;
    }
    
    const action = isReusing ? 'Reutilizar' : 'Iniciar';
    if (!confirm(`${action} esta fermentação?`)) return;
    
    try {
        await apiRequest('configurations/status', {
            method: 'PUT',
            body: JSON.stringify({ config_id: configId, status: 'active' })
        });
        
        await apiRequest('active/activate', {
            method: 'POST',
            body: JSON.stringify({ config_id: configId })
        });
        
        alert('Fermentação iniciada com sucesso!');
        await loadConfigurations();
        
    } catch (error) {
        console.error('Erro ao iniciar fermentação:', error);
        alert('Erro ao iniciar fermentação: ' + error.message);
    }
}

async function pauseFermentation(configId) {
    if (!confirm('Pausar esta fermentação?')) return;
    
    try {
        await apiRequest('configurations/status', {
            method: 'PUT',
            body: JSON.stringify({ config_id: configId, status: 'paused' })
        });
        
        await apiRequest('active/deactivate', { method: 'POST' });
        
        alert('Fermentação pausada!');
        await loadConfigurations();
    } catch (error) {
        console.error('Erro ao pausar fermentação:', error);
        alert('Erro: ' + error.message);
    }
}

async function resumeFermentation(configId) {
    const activeData = await apiRequest('active');
    if (activeData.active) {
        alert('Já existe uma fermentação ativa! Para retomar esta, primeiro pause a fermentação atual.');
        return;
    }
    
    if (!confirm('Retomar esta fermentação?')) return;
    
    try {
        await apiRequest('configurations/status', {
            method: 'PUT',
            body: JSON.stringify({ config_id: configId, status: 'active' })
        });
        
        await apiRequest('active/activate', {
            method: 'POST',
            body: JSON.stringify({ config_id: configId })
        });
        
        alert('Fermentação retomada!');
        await loadConfigurations();
    } catch (error) {
        console.error('Erro ao retomar fermentação:', error);
        alert('Erro: ' + error.message);
    }
}

async function completeFermentation(configId) {
    if (!confirm('Marcar esta fermentação como concluída?')) return;
    
    try {
        await apiRequest('configurations/status', {
            method: 'PUT',
            body: JSON.stringify({ config_id: configId, status: 'completed' })
        });
        
        const activeData = await apiRequest('active');
        if (activeData.active && activeData.id == configId) {
            await apiRequest('active/deactivate', { method: 'POST' });
        }
        
        alert('Fermentação marcada como concluída!');
        await loadConfigurations();
    } catch (error) {
        console.error('Erro ao concluir fermentação:', error);
        alert('Erro: ' + error.message);
    }
}

async function deleteConfig(configId) {
    const config = savedConfigs.find(c => c.id == configId);
    if (!config) return;
    
    if (config.status === 'active') {
        alert('Não é possível excluir uma fermentação ativa. Primeiro pause ou conclua a fermentação.');
        return;
    }
    
    if (!confirm('Tem certeza que deseja excluir esta configuração?')) return;
    
    try {
        await apiRequest('configurations/delete', {
            method: 'DELETE',
            body: JSON.stringify({ config_id: configId })
        });
        
        alert('Configuração excluída com sucesso!');
        await loadConfigurations();
    } catch (error) {
        console.error('Erro ao excluir:', error);
        alert('Erro ao excluir: ' + error.message);
    }
}

function refreshData() {
    loadConfigurations();
}

// ========== FUNÇÕES AUXILIARES ==========
function getStageSummary(stage) {
    if (!stage) return '';
    
    switch(stage.type) {
        case 'temperature':
            return `Manter ${stage.target_temp}°C por ${stage.duration || 7} dias`;
        case 'gravity':
            return `Manter ${stage.target_temp}°C até atingir ${stage.target_gravity} SG`;
        case 'gravity_time':
            return `Manter ${stage.target_temp}°C até atingir ${stage.target_gravity} SG ou ${stage.max_duration} dias`;
        case 'ramp':
            const direction = stage.direction === 'up' ? 'subir' : 'descer';
            const rampTimeDisplay = stage.ramp_time < 24 
                ? `${stage.ramp_time} horas` 
                : `${(stage.ramp_time / 24).toFixed(1)} dias`;
            return `Rampa: ${direction} de ${stage.start_temp}°C para ${stage.target_temp}°C em ${rampTimeDisplay}`;
        default:
            return '';
    }
}

function getStageTypeLabel(type) {
    switch(type) {
        case 'temperature': return 'Temperatura x Tempo';
        case 'gravity': return 'Gravidade Alvo';
        case 'gravity_time': return 'Gravidade x Tempo';
        case 'ramp': return 'Rampa de Temperatura';
        default: return '';
    }
}

// ========== TEMPLATES HTML ==========
const stageTemplate = (stage, index) => {
    const isRamp = stage.type === 'ramp';
    
    return `
    <div class="border border-gray-200 rounded-lg p-6 mb-4 ${isRamp ? 'bg-gradient-to-r from-blue-50 to-indigo-50 border-blue-200' : 'bg-gray-50'}">
        <div class="flex justify-between items-start mb-4">
            <h3 class="text-lg font-semibold ${isRamp ? 'text-blue-800' : 'text-gray-700'}">
                ${isRamp ? '<i class="fas fa-chart-line text-blue-600 mr-2"></i>' : ''}
                ${isRamp ? 'Rampa de Temperatura' : `Etapa ${Math.ceil((index + 2) / 2)}`}
                ${isRamp ? '' : ` (${getStageTypeLabel(stage.type)})`}
            </h3>
            <div class="flex gap-2">
                ${index > 0 ? `
                    <button onclick="moveStage(${index}, 'up')" class="btn-small">
                        <i class="fas fa-chevron-up"></i>
                    </button>
                ` : ''}
                ${index < stages.length - 1 ? `
                    <button onclick="moveStage(${index}, 'down')" class="btn-small">
                        <i class="fas fa-chevron-down"></i>
                    </button>
                ` : ''}
                <button onclick="removeStage(${stage.id})" class="btn-small btn-danger">
                    <i class="fas fa-trash"></i>
                </button>
            </div>
        </div>

        ${isRamp ? `
            <div class="space-y-4">
                <div class="grid grid-cols-2 gap-4">
                    <div>
                        <label class="block text-sm font-medium text-gray-700 mb-2">Temperatura Inicial (°C)</label>
                        <input type="number" step="0.1" value="${stage.startTemp}"
                               onchange="updateStage(${stage.id}, 'startTemp', parseFloat(this.value))"
                               class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                    </div>
                    <div>
                        <label class="block text-sm font-medium text-gray-700 mb-2">Temperatura Final (°C)</label>
                        <input type="number" step="0.1" value="${stage.targetTemp}"
                               onchange="updateStage(${stage.id}, 'targetTemp', parseFloat(this.value))"
                               class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                    </div>
                </div>
                
                <div class="grid grid-cols-2 gap-4">
                    <div>
                        <label class="block text-sm font-medium text-gray-700 mb-2">Tempo da Rampa (horas)</label>
                        <input type="number" min="1" step="1" value="${stage.rampTime}"
                               onchange="updateStage(${stage.id}, 'rampTime', parseFloat(this.value))"
                               class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                        <p class="text-xs text-gray-500 mt-1">${(stage.rampTime / 24).toFixed(1)} dias</p>
                    </div>
                    <div>
                        <label class="block text-sm font-medium text-gray-700 mb-2">Taxa (°C/dia)</label>
                        <input type="number" step="0.1" value="${stage.actualRate.toFixed(1)}"
                               class="w-full px-4 py-2 border border-gray-300 rounded-lg bg-gray-100"
                               readonly>
                    </div>
                </div>
            </div>
        ` : `
            <div class="space-y-4">
                <div>
                    <label class="block text-sm font-medium text-gray-700 mb-2">Tipo de Etapa</label>
                    <select onchange="updateStage(${stage.id}, 'type', this.value)" 
                            class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                        <option value="temperature" ${stage.type === 'temperature' ? 'selected' : ''}>
                            Temperatura x Tempo
                        </option>
                        <option value="gravity" ${stage.type === 'gravity' ? 'selected' : ''}>
                            Gravidade Alvo
                        </option>
                        <option value="gravity_time" ${stage.type === 'gravity_time' ? 'selected' : ''}>
                            Gravidade x Tempo
                        </option>
                    </select>
                </div>

                <div class="grid grid-cols-2 gap-4">
                    <div>
                        <label class="block text-sm font-medium text-gray-700 mb-2">Temperatura Alvo (°C)</label>
                        <input type="number" step="0.5" value="${stage.targetTemp}"
                               onchange="updateStage(${stage.id}, 'targetTemp', parseFloat(this.value))"
                               class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                    </div>

                    ${stage.type === 'temperature' ? `
                        <div>
                            <label class="block text-sm font-medium text-gray-700 mb-2">Duração (dias)</label>
                            <input type="number" min="1" value="${stage.duration}"
                                   onchange="updateStage(${stage.id}, 'duration', parseInt(this.value))"
                                   class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                        </div>
                    ` : ''}

                    ${(stage.type === 'gravity' || stage.type === 'gravity_time') ? `
                        <div>
                            <label class="block text-sm font-medium text-gray-700 mb-2">Gravidade Alvo (SG)</label>
                            <input type="number" step="0.001" value="${stage.targetGravity}"
                                   onchange="updateStage(${stage.id}, 'targetGravity', parseFloat(this.value))"
                                   class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                        </div>
                    ` : ''}

                    ${stage.type === 'gravity_time' ? `
                        <div>
                            <label class="block text-sm font-medium text-gray-700 mb-2">Duração Máxima (dias)</label>
                            <input type="number" min="1" value="${stage.maxDuration}"
                                   onchange="updateStage(${stage.id}, 'maxDuration', parseInt(this.value))"
                                   class="w-full px-4 py-2 border border-gray-300 rounded-lg">
                        </div>
                    ` : ''}
                </div>
            </div>
        `}

        <div class="bg-blue-50 rounded p-3 text-sm text-gray-700 mt-4">
            <strong>Resumo:</strong> ${getStageSummary(stage)}
        </div>
    </div>
`};

const savedConfigTemplate = (config) => {
    const isActive = config.status === 'active';
    const isPaused = config.status === 'paused';
    const isCompleted = config.status === 'completed';
    
    return `
    <div class="border border-gray-200 rounded-lg p-6 mb-4 ${isActive ? 'bg-green-50 border-green-300' : ''}">
        <div class="flex justify-between items-start mb-4">
            <div>
                <h3 class="text-xl font-semibold ${isActive ? 'text-green-800' : 'text-gray-800'}">
                    ${config.name}
                    ${isActive ? ' <span class="text-sm bg-green-100 text-green-800 px-2 py-1 rounded">ATIVA</span>' : ''}
                    ${isCompleted ? ' <span class="text-sm bg-gray-100 text-gray-800 px-2 py-1 rounded">CONCLUÍDA</span>' : ''}
                </h3>
                <div class="space-y-2 mt-4">
                    <p class="text-sm font-medium text-gray-700">
                        ${config.stages?.length || 0} etapa(s)
                    </p>
                    ${(config.stages || []).map((stage, idx) => `
                        <div class="text-sm ${stage.type === 'ramp' ? 'text-blue-600 pl-4 border-l-2 border-blue-300' : 'text-gray-600 pl-4 border-l-2 border-green-300'}">
                            ${idx + 1}. ${getStageTypeLabel(stage.type)} - ${getStageSummary(stage)}
                        </div>
                    `).join('')}
                </div>
                <p class="text-sm text-gray-500 mt-4">
                    Criada: ${new Date(config.created_at).toLocaleString('pt-BR')}
                    ${config.started_at ? `<br>Iniciada: ${new Date(config.started_at).toLocaleString('pt-BR')}` : ''}
                </p>
                <span class="inline-block mt-2 px-3 py-1 rounded-full text-sm font-medium ${
                    isActive ? 'bg-green-100 text-green-800' :
                    isCompleted ? 'bg-gray-100 text-gray-800' :
                    isPaused ? 'bg-yellow-100 text-yellow-800' :
                    'bg-blue-100 text-blue-800'
                }">
                    ${isActive ? 'Ativa' :
                     isCompleted ? 'Concluída' :
                     isPaused ? 'Pausada' : 'Não Ativa'}
                </span>
            </div>
            <div class="flex flex-col gap-2">
                ${(!isActive && !isPaused) ? `
                    <button onclick="startFermentation('${config.id}', ${isCompleted})" class="btn-compact btn-primary">
                        <i class="fas fa-play mr-1"></i> ${isCompleted ? 'Reutilizar' : 'Iniciar'}
                    </button>
                ` : ''}
                
                ${isActive ? `
                    <button onclick="pauseFermentation('${config.id}')" class="btn-compact btn-warning">
                        <i class="fas fa-pause mr-1"></i> Pausar
                    </button>
                ` : ''}
                
                ${isPaused ? `
                    <button onclick="resumeFermentation('${config.id}')" class="btn-compact btn-success">
                        <i class="fas fa-play mr-1"></i> Retomar
                    </button>
                ` : ''}
                
                ${(isActive || isPaused) ? `
                    <button onclick="completeFermentation('${config.id}')" class="btn-compact btn-secondary">
                        <i class="fas fa-flag-checkered mr-1"></i> Concluir
                    </button>
                ` : ''}
                
                ${!isActive ? `
                    <button onclick="deleteConfig('${config.id}')" class="btn-compact btn-danger">
                        <i class="fas fa-trash mr-1"></i> Excluir
                    </button>
                ` : ''}
            </div>
        </div>
    </div>
`};

// ========== FUNÇÕES DE RENDERIZAÇÃO ==========
function renderStages() {
    const container = document.getElementById('stages-container');
    const noStagesMsg = document.getElementById('no-stages-message');
    
    if (!container || !noStagesMsg) return;
    
    if (stages.length === 0) {
        container.innerHTML = '';
        noStagesMsg.style.display = 'block';
    } else {
        container.innerHTML = stages.map((stage, index) => stageTemplate(stage, index)).join('');
        noStagesMsg.style.display = 'none';
    }
}

async function renderSavedConfigs() {
    const container = document.getElementById('saved-configs');
    if (!container) return;
    
    const activeData = await apiRequest('active');
    const hasActive = activeData.active;
    
    if (savedConfigs.length === 0) {
        container.innerHTML = `
            <div class="text-center py-8 text-gray-500">
                Nenhuma configuração salva ainda.
            </div>
        `;
    } else {
        container.innerHTML = savedConfigs.map(config => savedConfigTemplate(config)).join('');
        updateActiveWarning(hasActive);
    }
}

function updateActiveWarning(hasActive) {
    const warning = document.getElementById('active-warning');
    const warningText = document.getElementById('active-warning-text');
    
    if (!warning || !warningText) return;
    
    if (hasActive) {
        const activeConfig = savedConfigs.find(c => c.status === 'active');
        if (activeConfig) {
            warningText.textContent = `Fermentação ativa: "${activeConfig.name}". Para iniciar uma nova, primeiro pause ou conclua a fermentação atual.`;
        } else {
            warningText.textContent = 'Existe uma fermentação ativa no sistema.';
        }
        warning.style.display = 'flex';
    } else {
        warning.style.display = 'none';
    }
}

// ========== EXPORTAÇÃO PARA ESCOPO GLOBAL ==========
window.addStage = addStage;
window.addStageWithRamp = addStageWithRamp;
window.removeStage = removeStage;
window.updateStage = updateStage;
window.moveStage = moveStage;
window.saveConfiguration = saveConfiguration;
window.startFermentation = startFermentation;
window.pauseFermentation = pauseFermentation;
window.resumeFermentation = resumeFermentation;
window.completeFermentation = completeFermentation;
window.deleteConfig = deleteConfig;
window.refreshData = refreshData;
window.login = login;
window.logout = logout;
window.openRampModal = openRampModal;
window.closeRampModal = closeRampModal;
window.updateRampVisualization = updateRampVisualization;
window.saveRampConfiguration = saveRampConfiguration;

// ========== INICIALIZAÇÃO ==========
document.addEventListener('DOMContentLoaded', async () => {
    console.log('Sistema de Configuração carregado');
    
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