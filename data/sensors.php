<?php
// api/esp/sensors.php - Gerenciamento completo de sensores
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA';

$action = $_GET['action'] ?? null;

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // ==================== PING (verificar conexão) ====================
    if ($action === 'ping') {
        echo json_encode(['success' => true, 'message' => 'Server online']);
        exit;
    }
    
    // ==================== REQUEST SCAN (solicita scan ao ESP) ====================
    if ($action === 'request_scan' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        // Define flag para ESP8266 fazer scan
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, last_seen)
            VALUES ('scan_command', 'command', NOW())
            ON DUPLICATE KEY UPDATE last_seen = NOW()
        ");
        $stmt->execute();
        
        echo json_encode(['success' => true, 'message' => 'Scan requested']);
        exit;
    }
    
    // ==================== GET DETECTED (sensores detectados) ====================
    if ($action === 'get_detected') {
        // Busca últimos sensores detectados (enviados pelo ESP)
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
            echo json_encode(['success' => true, 'sensors' => $sensors]);
        } else {
            echo json_encode(['success' => false, 'sensors' => []]);
        }
        exit;
    }
    
    // ==================== SAVE DETECTED (ESP envia sensores detectados) ====================
    if ($action === 'save_detected' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        $sensors = $input['sensors'] ?? [];
        
        if (empty($sensors)) {
            http_response_code(400);
            echo json_encode(['error' => 'No sensors provided']);
            exit;
        }
        
        // Salva no banco
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, sensor_data, last_seen)
            VALUES ('detected_sensors', 'sensors', ?, NOW())
            ON DUPLICATE KEY UPDATE 
                sensor_data = VALUES(sensor_data),
                last_seen = NOW()
        ");
        $stmt->execute([json_encode($sensors)]);
        
        echo json_encode(['success' => true, 'count' => count($sensors)]);
        exit;
    }
    
    // ==================== GET ASSIGNED (sensores configurados) ====================
    if ($action === 'get_assigned') {
        // Busca sensores configurados
        $stmt = $pdo->prepare("
            SELECT config_key, config_value
            FROM system_config
            WHERE config_key IN ('sensor_fermentador', 'sensor_geladeira')
        ");
        $stmt->execute();
        $results = $stmt->fetchAll(PDO::FETCH_ASSOC);
        
        $sensors = [];
        foreach ($results as $row) {
            $sensors[$row['config_key']] = $row['config_value'];
        }
        
        echo json_encode(['success' => true, 'sensors' => $sensors]);
        exit;
    }
    
    // ==================== ASSIGN (configurar sensor) ====================
    if ($action === 'assign' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        $address = $input['address'] ?? null;
        $role = $input['role'] ?? null;
        
        if (!$address || !$role) {
            http_response_code(400);
            echo json_encode(['error' => 'Address and role required']);
            exit;
        }
        
        // Valida role
        if (!in_array($role, ['sensor_fermentador', 'sensor_geladeira'])) {
            http_response_code(400);
            echo json_encode(['error' => 'Invalid role']);
            exit;
        }
        
        // Salva configuração
        $stmt = $pdo->prepare("
            INSERT INTO system_config (config_key, config_value, updated_at)
            VALUES (?, ?, NOW())
            ON DUPLICATE KEY UPDATE 
                config_value = VALUES(config_value),
                updated_at = NOW()
        ");
        $stmt->execute([$role, $address]);
        
        // Notifica ESP8266 (flag para recarregar)
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, last_seen)
            VALUES ('config_updated', 'command', NOW())
            ON DUPLICATE KEY UPDATE last_seen = NOW()
        ");
        $stmt->execute();
        
        echo json_encode(['success' => true]);
        exit;
    }
    
    // ==================== REMOVE (remover sensor) ====================
    if ($action === 'remove' && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $input = json_decode(file_get_contents('php://input'), true);
        $role = $input['role'] ?? null;
        
        if (!$role) {
            http_response_code(400);
            echo json_encode(['error' => 'Role required']);
            exit;
        }
        
        // Remove configuração
        $stmt = $pdo->prepare("DELETE FROM system_config WHERE config_key = ?");
        $stmt->execute([$role]);
        
        // Notifica ESP8266
        $stmt = $pdo->prepare("
            INSERT INTO devices (device_id, device_type, last_seen)
            VALUES ('config_updated', 'command', NOW())
            ON DUPLICATE KEY UPDATE last_seen = NOW()
        ");
        $stmt->execute();
        
        echo json_encode(['success' => true]);
        exit;
    }
    
    // ==================== GET TEMPERATURES (temperaturas atuais) ====================
    if ($action === 'get_temperatures') {
        // Busca última leitura de temperatura
        $stmt = $pdo->prepare("
            SELECT temp_fermenter, temp_fridge, reading_timestamp
            FROM readings
            ORDER BY reading_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute();
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result) {
            echo json_encode([
                'success' => true,
                'temperatures' => [
                    'fermenter' => (float)$result['temp_fermenter'],
                    'fridge' => (float)$result['temp_fridge']
                ],
                'timestamp' => $result['reading_timestamp']
            ]);
        } else {
            echo json_encode([
                'success' => false,
                'temperatures' => [
                    'fermenter' => null,
                    'fridge' => null
                ]
            ]);
        }
        exit;
    }
    
    // ==================== GET CONFIG FOR ESP (ESP busca configuração) ====================
    if ($action === 'get_config_for_esp') {
        // Retorna configuração de sensores para o ESP carregar na EEPROM
        $stmt = $pdo->prepare("
            SELECT config_key, config_value
            FROM system_config
            WHERE config_key IN ('sensor_fermentador', 'sensor_geladeira')
        ");
        $stmt->execute();
        $results = $stmt->fetchAll(PDO::FETCH_ASSOC);
        
        $config = [];
        foreach ($results as $row) {
            $config[$row['config_key']] = $row['config_value'];
        }
        
        echo json_encode(['success' => true, 'config' => $config]);
        exit;
    }
    
    // Ação não reconhecida
    http_response_code(400);
    echo json_encode(['error' => 'Invalid action']);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}