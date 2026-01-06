-- Tabela de usuários (autenticação)
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP NULL,
    is_active BOOLEAN DEFAULT TRUE
);

-- Tabela de configurações de fermentação
CREATE TABLE configurations (
    id INT PRIMARY KEY AUTO_INCREMENT,
    user_id INT,
    name VARCHAR(100) NOT NULL,
    status ENUM('pending', 'active', 'paused', 'completed') DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMP NULL,
    paused_at TIMESTAMP NULL,
    completed_at TIMESTAMP NULL,
    times_used INT DEFAULT 0,
    current_stage_index INT DEFAULT 0,
    current_target_temp DECIMAL(4,1) NULL,
    
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_status (status),
    INDEX idx_user (user_id)
);

-- Tabela de etapas de fermentação (incluindo rampas)
CREATE TABLE stages (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT NOT NULL,
    stage_index INT NOT NULL,
    type ENUM('temperature', 'gravity', 'gravity_time', 'ramp') NOT NULL,
    
    -- Campos gerais
    target_temp DECIMAL(4,1),
    duration INT, -- em dias
    target_gravity DECIMAL(5,3),
    max_duration INT, -- em dias
    
    -- Campos específicos para rampas
    start_temp DECIMAL(4,1),
    ramp_time INT, -- em horas
    max_ramp_rate DECIMAL(4,2), -- taxa máxima em °C/dia
    actual_rate DECIMAL(4,2), -- taxa real em °C/dia
    direction ENUM('up', 'down'),
    
    -- Status e controle
    status ENUM('pending', 'running', 'completed', 'waiting') DEFAULT 'pending',
    start_time TIMESTAMP NULL,
    end_time TIMESTAMP NULL,
    target_reached_time TIMESTAMP NULL,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    INDEX idx_config (config_id),
    INDEX idx_config_stage (config_id, stage_index),
    UNIQUE KEY unique_config_stage (config_id, stage_index)
);

-- Tabela de leituras (sensores)
CREATE TABLE readings (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT NOT NULL,
    reading_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    temp_fridge DECIMAL(4,1) NOT NULL, -- temperatura da geladeira
    temp_fermenter DECIMAL(4,1) NOT NULL, -- temperatura do fermentador
    temp_target DECIMAL(4,1) NOT NULL, -- temperatura alvo atual
    gravity DECIMAL(5,3) NULL, -- gravidade específica
    gravity_temp_correction DECIMAL(4,1) NULL, -- temperatura para correção da gravidade
    battery_percent DECIMAL(5,2) NULL, -- bateria do sensor
    
    -- Coluna gerada para otimização de consultas por data
    reading_date DATE GENERATED ALWAYS AS (DATE(reading_timestamp)) STORED,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    INDEX idx_config_timestamp (config_id, reading_timestamp),
    INDEX idx_timestamp (reading_timestamp),
    INDEX idx_config_date (config_id, reading_date)
);

-- Tabela de estado do controlador
CREATE TABLE controller_states (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT NOT NULL,
    state_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    setpoint DECIMAL(4,1) NOT NULL,
    cooling BOOLEAN DEFAULT FALSE,
    heating BOOLEAN DEFAULT FALSE,
    is_running BOOLEAN DEFAULT TRUE,
    last_cycle_time TIMESTAMP NULL,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    INDEX idx_config (config_id),
    INDEX idx_timestamp (state_timestamp)
);

-- Tabela de estado da fermentação
CREATE TABLE fermentation_states (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT NOT NULL,
    state_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    time_remaining_value INT NULL,
    time_remaining_unit ENUM('horas', 'dias') NULL,
    current_target_temp DECIMAL(4,1) NULL,
    target_reached_time TIMESTAMP NULL,
    is_target_reached BOOLEAN DEFAULT FALSE,
    
    -- Armazena timers das etapas como JSON
    stage_timers JSON,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    INDEX idx_config (config_id),
    INDEX idx_timestamp (state_timestamp)
);

-- Tabela de fermentações ativas (equivalente ao nó 'active' no Firebase)
CREATE TABLE active_fermentations (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT NOT NULL UNIQUE,
    activated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    deactivated_at TIMESTAMP NULL,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    INDEX idx_active (activated_at, deactivated_at)
);

-- Tabela de histórico de ações
CREATE TABLE action_history (
    id INT PRIMARY KEY AUTO_INCREMENT,
    user_id INT,
    config_id INT,
    action_type VARCHAR(50) NOT NULL,
    action_details TEXT,
    action_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL,
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE SET NULL,
    INDEX idx_user_action (user_id, action_type),
    INDEX idx_timestamp (action_timestamp)
);

-- Tabela de dispositivos/sensores
CREATE TABLE devices (
    id INT PRIMARY KEY AUTO_INCREMENT,
    device_id VARCHAR(50) UNIQUE NOT NULL,
    device_name VARCHAR(100),
    device_type ENUM('controller', 'sensor', 'both') NOT NULL,
    last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    firmware_version VARCHAR(20),
    ip_address VARCHAR(45),
    is_online BOOLEAN DEFAULT FALSE,
    
    INDEX idx_device_id (device_id),
    INDEX idx_last_seen (last_seen)
);

-- Tabela de alertas e notificações
CREATE TABLE alerts (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_id INT,
    device_id INT,
    alert_type ENUM('temperature', 'gravity', 'device', 'stage_completion', 'error') NOT NULL,
    alert_level ENUM('info', 'warning', 'critical') DEFAULT 'warning',
    message TEXT NOT NULL,
    is_read BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    resolved_at TIMESTAMP NULL,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE,
    INDEX idx_config_alert (config_id, alert_type),
    INDEX idx_unread (is_read, created_at)
);

-- Índices adicionais para otimização
CREATE INDEX idx_stages_status ON stages (status);
CREATE INDEX idx_configurations_status_user ON configurations (status, user_id);
CREATE INDEX idx_readings_timestamp_desc ON readings (reading_timestamp DESC);
CREATE INDEX idx_fermentation_states_config_time ON fermentation_states (config_id, state_timestamp DESC);
CREATE INDEX idx_controller_states_config_time ON controller_states (config_id, state_timestamp DESC);

-- Tabela de cache para cálculos frequentes (opcional, para performance)
CREATE TABLE fermentation_stats (
    config_id INT PRIMARY KEY,
    avg_temp_fermenter DECIMAL(4,2),
    avg_temp_fridge DECIMAL(4,2),
    min_gravity DECIMAL(5,3),
    max_gravity DECIMAL(5,3),
    total_readings INT,
    first_reading TIMESTAMP,
    last_reading TIMESTAMP,
    calculated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE
);

-- Tabela para armazenar configurações do sistema (incluindo sensores)
CREATE TABLE IF NOT EXISTS system_config (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_key VARCHAR(50) UNIQUE NOT NULL,
    config_value TEXT NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_key (config_key)
);

-- Atualiza tabela devices para incluir sensor_data
ALTER TABLE devices 
ADD COLUMN IF NOT EXISTS sensor_data TEXT AFTER firmware_version;

-- Inserir configurações padrão de sensores (vazias inicialmente)
INSERT INTO system_config (config_key, config_value) VALUES
('sensor_fermentador', '')
ON DUPLICATE KEY UPDATE config_key = config_key;

INSERT INTO system_config (config_key, config_value) VALUES
('sensor_geladeira', '')
ON DUPLICATE KEY UPDATE config_key = config_key;

-- View para facilitar consulta de sensores configurados
CREATE OR REPLACE VIEW v_sensors_config AS
SELECT 
    config_key as sensor_role,
    config_value as sensor_address,
    updated_at
FROM system_config
WHERE config_key IN ('sensor_fermentador', 'sensor_geladeira')
AND config_value != '';