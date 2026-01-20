<?php
// api/esp/sensors.php - Gerenciamento de sensores com CLEAR_EEPROM
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

$action = $_GET['action'] ?? null;

error_log("sensors.php - Action: $action, Method: {$_SERVER['REQUEST_METHOD']}");

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // ==================== PING ====================
    if ($action === 'ping') {
        echo json_encode(['success' => true, 'message' => 'Server online']);
        exit;
    }
    
    // ==================== REQUEST SCAN ====================
    if ($action === 'request_scan' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, last_seen, is_online)
            VALUES ('scan_command', 'command', NOW(), TRUE)
            ON DUPLICATE KEY UPDATE 
                last_seen = NOW(),
                is_online = TRUE
        ");
        $stmt->execute();
        
        echo json_encode([
            'success' => true, 
            'message' => 'Scan command registered'
        ]);
        exit;
    }
    
    // ==================== SAVE DETECTED ====================
    if ($action === 'save_detected' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        
        error_log("save_detected - Recebido: " . print_r($input, true));
        
        $sensors = $input['sensors'] ?? [];
        
        if (empty($sensors)) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'No sensors provided']);
            exit;
        }
        
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, sensor_data, last_seen, is_online)
            VALUES ('detected_sensors', 'sensors', ?, NOW(), TRUE)
            ON DUPLICATE KEY UPDATE 
                sensor_data = VALUES(sensor_data),
                last_seen = NOW(),
                is_online = TRUE
        ");
        $stmt->execute([json_encode($sensors)]);
        
        try {
            $stmt = $pdo->prepare("
                INSERT INTO detected_sensors (address, detected_at, last_seen) 
                VALUES (?, NOW(), NOW())
                ON DUPLICATE KEY UPDATE last_seen = NOW()
            ");
            
            foreach ($sensors as $address) {
                $stmt->execute([$address]);
            }
        } catch (PDOException $e) {
            error_log("Erro ao criar detected_sensors: " . $e->getMessage());
        }
        
        echo json_encode([
            'success' => true, 
            'count' => count($sensors),
            'message' => 'Sensors saved successfully'
        ]);
        exit;
    }
    
    // ==================== GET DETECTED ====================
    if ($action === 'get_detected') {
        $stmt = $pdo->prepare("
            SELECT sensor_data
            FROM devices
            WHERE device_id = 'detected_sensors'
            AND device_type = 'sensors'
            ORDER BY last_seen DESC
            LIMIT 1
        ");
        $stmt->execute();
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result && $result['sensor_data']) {
            $sensors = json_decode($result['sensor_data'], true);
            echo json_encode([
                'success' => true, 
                'sensors' => $sensors,
                'count' => count($sensors)
            ]);
        } else {
            echo json_encode([
                'success' => true, 
                'sensors' => [],
                'count' => 0,
                'message' => 'No sensors detected yet'
            ]);
        }
        exit;
    }
    
    // ==================== GET ASSIGNED ====================
    if ($action === 'get_assigned') {
        $stmt = $pdo->prepare("
            SELECT config_key, config_value
            FROM system_config
            WHERE config_key IN ('sensor_fermentador', 'sensor_geladeira')
        ");
        $stmt->execute();
        $results = $stmt->fetchAll(PDO::FETCH_ASSOC);
        
        $sensors = [
            'sensor_fermentador' => null,
            'sensor_geladeira' => null
        ];
        
        foreach ($results as $row) {
            $key = $row['config_key'];
            $value = trim($row['config_value']);
            
            if (!empty($value)) {
                $sensors[$key] = $value;
            }
        }
        
        error_log("Sensores configurados: " . json_encode($sensors));
        
        $hasConfig = !empty($sensors['sensor_fermentador']) || !empty($sensors['sensor_geladeira']);
        
        echo json_encode([
            'success' => true,
            'sensors' => $sensors,
            'message' => $hasConfig ? 'Sensors found' : 'No sensors configured'
        ]);
        exit;
    }
    
    // ==================== ASSIGN (COM CLEAR_EEPROM) ====================
    if ($action === 'assign' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        
        error_log("assign - Recebido: " . print_r($input, true));
        
        $updates = [];
        
        // Formato novo (um sensor por vez)
        if (isset($input['address']) && isset($input['role'])) {
            $address = $input['address'];
            $role = $input['role'];
            
            if (!in_array($role, ['sensor_fermentador', 'sensor_geladeira'])) {
                http_response_code(400);
                echo json_encode(['success' => false, 'error' => 'Invalid role']);
                exit;
            }
            
            $updates[$role] = $address;
        }
        // Formato antigo (múltiplos sensores)
        else {
            if (isset($input['sensor_fermentador'])) {
                $updates['sensor_fermentador'] = $input['sensor_fermentador'];
            }
            if (isset($input['sensor_geladeira'])) {
                $updates['sensor_geladeira'] = $input['sensor_geladeira'];
            }
        }
        
        if (empty($updates)) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'No sensors to assign']);
            exit;
        }
        
        // Atualiza no banco
        $stmt = $pdo->prepare("
            INSERT INTO system_config (config_key, config_value, updated_at)
            VALUES (?, ?, NOW())
            ON DUPLICATE KEY UPDATE 
                config_value = VALUES(config_value),
                updated_at = NOW()
        ");
        
        $count = 0;
        foreach ($updates as $role => $address) {
            if (!empty($address)) {
                $stmt->execute([$role, $address]);
                $count++;
            }
        }
        
        // ===== ENVIA COMANDO CLEAR_EEPROM PARA O ESP =====
        try {
            // Registra comando CLEAR_EEPROM
            $cmdStmt = $pdo->prepare("
                INSERT INTO esp_commands (command, status, created_at)
                VALUES ('CLEAR_EEPROM', 'pending', NOW())
            ");
            $cmdStmt->execute();
            
            error_log("Comando CLEAR_EEPROM registrado - ID: " . $pdo->lastInsertId());
            
            echo json_encode([
                'success' => true,
                'updated' => $count,
                'clear_eeprom_sent' => true,
                'message' => "$count sensor(s) configured. EEPROM clear command sent."
            ]);
        } catch (PDOException $e) {
            error_log("Erro ao enviar CLEAR_EEPROM: " . $e->getMessage());
            
            // Retorna sucesso mesmo se falhar o comando (sensores foram salvos)
            echo json_encode([
                'success' => true,
                'updated' => $count,
                'clear_eeprom_sent' => false,
                'message' => "$count sensor(s) configured. Warning: Could not send EEPROM clear."
            ]);
        }
        exit;
    }
    
    // ==================== GET PENDING COMMANDS (ESP busca comandos) ====================
    if ($action === 'get_commands') {
        try {
            // Busca comandos pendentes
            $stmt = $pdo->prepare("
                SELECT id, command, created_at
                FROM esp_commands
                WHERE status = 'pending'
                ORDER BY created_at ASC
                LIMIT 10
            ");
            $stmt->execute();
            $commands = $stmt->fetchAll(PDO::FETCH_ASSOC);
            
            echo json_encode([
                'success' => true,
                'commands' => $commands,
                'count' => count($commands)
            ]);
        } catch (PDOException $e) {
            error_log("Erro ao buscar comandos: " . $e->getMessage());
            echo json_encode([
                'success' => true,
                'commands' => [],
                'count' => 0
            ]);
        }
        exit;
    }
    
    // ==================== MARK COMMAND EXECUTED (ESP confirma execução) ====================
    if ($action === 'mark_executed' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        $commandId = $input['command_id'] ?? null;
        
        if (!$commandId) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'Command ID required']);
            exit;
        }
        
        try {
            $stmt = $pdo->prepare("
                UPDATE esp_commands
                SET status = 'executed', executed_at = NOW()
                WHERE id = ? AND status = 'pending'
            ");
            $stmt->execute([$commandId]);
            
            echo json_encode([
                'success' => true,
                'message' => 'Command marked as executed'
            ]);
        } catch (PDOException $e) {
            error_log("Erro ao marcar comando: " . $e->getMessage());
            http_response_code(500);
            echo json_encode(['success' => false, 'error' => 'Database error']);
        }
        exit;
    }
    
    // ==================== REMOVE ====================
    if ($action === 'remove' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        $role = $input['role'] ?? null;
        
        if (!$role) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'Role required']);
            exit;
        }
        
        if (!in_array($role, ['sensor_fermentador', 'sensor_geladeira'])) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'Invalid role']);
            exit;
        }
        
        $stmt = $pdo->prepare("
            UPDATE system_config 
            SET config_value = '', updated_at = NOW()
            WHERE config_key = ?
        ");
        $stmt->execute([$role]);
        
        // TAMBÉM envia CLEAR_EEPROM ao remover
        try {
            $cmdStmt = $pdo->prepare("
                INSERT INTO esp_commands (command, status, created_at)
                VALUES ('CLEAR_EEPROM', 'pending', NOW())
            ");
            $cmdStmt->execute();
        } catch (PDOException $e) {
            error_log("Erro ao enviar CLEAR_EEPROM on remove: " . $e->getMessage());
        }
        
        echo json_encode([
            'success' => true,
            'message' => 'Sensor removed. EEPROM clear command sent.'
        ]);
        exit;
    }
    
    // ==================== UPDATE TEMPERATURES ====================
    if ($action === 'update_temperatures' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        
        error_log("update_temperatures - Recebido: " . print_r($input, true));
        
        $tempFermenter = $input['temp_fermenter'] ?? null;
        $tempFridge = $input['temp_fridge'] ?? null;
        
        if ($tempFermenter === null || $tempFridge === null) {
            http_response_code(400);
            echo json_encode(['success' => false, 'error' => 'Temperatures required']);
            exit;
        }
        
        $tempData = json_encode([
            'temp_fermenter' => (float)$tempFermenter,
            'temp_fridge' => (float)$tempFridge,
            'timestamp' => date('Y-m-d H:i:s')
        ]);
        
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, sensor_data, last_seen, is_online)
            VALUES ('current_temperatures', 'cache', ?, NOW(), TRUE)
            ON DUPLICATE KEY UPDATE 
                sensor_data = VALUES(sensor_data),
                last_seen = NOW(),
                is_online = TRUE
        ");
        $stmt->execute([$tempData]);
        
        try {
            $pdo->exec("
                CREATE TABLE IF NOT EXISTS sensor_cache (
                    id INT AUTO_INCREMENT PRIMARY KEY,
                    sensor_type ENUM('fermenter', 'fridge') UNIQUE NOT NULL,
                    temperature DECIMAL(4,1),
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                    INDEX idx_type (sensor_type)
                )
            ");
            
            $stmt = $pdo->prepare("
                INSERT INTO sensor_cache (sensor_type, temperature, updated_at)
                VALUES (?, ?, NOW())
                ON DUPLICATE KEY UPDATE 
                    temperature = VALUES(temperature),
                    updated_at = NOW()
            ");
            
            $stmt->execute(['fermenter', $tempFermenter]);
            $stmt->execute(['fridge', $tempFridge]);
        } catch (PDOException $e) {
            error_log("Erro ao criar sensor_cache: " . $e->getMessage());
        }
        
        echo json_encode([
            'success' => true,
            'message' => 'Temperatures updated'
        ]);
        exit;
    }
    
    // ==================== GET TEMPERATURES ====================
    if ($action === 'get_temperatures') {
        $stmt = $pdo->prepare("
            SELECT sensor_data, last_seen
            FROM devices
            WHERE device_id = 'current_temperatures'
            AND device_type = 'cache'
            AND last_seen >= DATE_SUB(NOW(), INTERVAL 1 MINUTE)
            ORDER BY last_seen DESC
            LIMIT 1
        ");
        $stmt->execute();
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result && $result['sensor_data']) {
            $tempData = json_decode($result['sensor_data'], true);
            echo json_encode([
                'success' => true,
                'temperatures' => [
                    'fermenter' => (float)$tempData['temp_fermenter'],
                    'fridge' => (float)$tempData['temp_fridge']
                ],
                'timestamp' => $tempData['timestamp'],
                'source' => 'cache'
            ]);
        } else {
            $stmt = $pdo->prepare("
                SELECT temp_fermenter, temp_fridge, reading_timestamp
                FROM readings
                ORDER BY reading_timestamp DESC
                LIMIT 1
            ");
            $stmt->execute();
            $reading = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if ($reading) {
                echo json_encode([
                    'success' => true,
                    'temperatures' => [
                        'fermenter' => (float)$reading['temp_fermenter'],
                        'fridge' => (float)$reading['temp_fridge']
                    ],
                    'timestamp' => $reading['reading_timestamp'],
                    'source' => 'readings'
                ]);
            } else {
                echo json_encode([
                    'success' => false,
                    'temperatures' => [
                        'fermenter' => null, 
                        'fridge' => null
                    ],
                    'message' => 'No temperature data available'
                ]);
            }
        }
        exit;
    }
    
    // ==================== AÇÃO NÃO RECONHECIDA ====================
    http_response_code(400);
    echo json_encode([
        'success' => false,
        'error' => 'Invalid action', 
        'received_action' => $action,
        'available_actions' => [
            'ping', 
            'request_scan', 
            'save_detected', 
            'get_detected', 
            'get_assigned', 
            'assign', 
            'remove', 
            'update_temperatures',
            'get_temperatures',
            'get_commands',
            'mark_executed'
        ]
    ]);
    
} catch (PDOException $e) {
    error_log("Database error in sensors.php: " . $e->getMessage());
    http_response_code(500);
    echo json_encode([
        'success' => false,
        'error' => 'Database error', 
        'details' => $e->getMessage()
    ]);
}