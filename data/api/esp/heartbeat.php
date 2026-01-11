<?php
// api/esp/heartbeat.php - Recebe heartbeat do ESP
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database connection error']);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $input = json_decode(file_get_contents('php://input'), true);
    
    $configId = $input['config_id'] ?? null;
    $status = $input['status'] ?? 'online';
    $uptimeSeconds = $input['uptime_seconds'] ?? 0;
    $tempFermenter = $input['temp_fermenter'] ?? null;
    $tempFridge = $input['temp_fridge'] ?? null;
    $coolerActive = isset($input['cooler_active']) ? (bool)$input['cooler_active'] : null;
    $heaterActive = isset($input['heater_active']) ? (bool)$input['heater_active'] : null;
    
    if (!$configId) {
        http_response_code(400);
        echo json_encode(['error' => 'config_id is required']);
        exit;
    }
    
    // Cria tabela se não existir
    try {
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS esp_heartbeat (
                id INT AUTO_INCREMENT PRIMARY KEY,
                config_id INT NOT NULL,
                status VARCHAR(20) DEFAULT 'online',
                uptime_seconds INT DEFAULT 0,
                temp_fermenter DECIMAL(5,2) DEFAULT NULL,
                temp_fridge DECIMAL(5,2) DEFAULT NULL,
                cooler_active BOOLEAN DEFAULT NULL,
                heater_active BOOLEAN DEFAULT NULL,
                heartbeat_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_config_id (config_id),
                INDEX idx_timestamp (heartbeat_timestamp DESC)
            )
        ");
    } catch (PDOException $e) {
        error_log("Error creating esp_heartbeat table: " . $e->getMessage());
    }
    
    // Insere heartbeat
    try {
        $stmt = $pdo->prepare("
            INSERT INTO esp_heartbeat (
                config_id, 
                status, 
                uptime_seconds, 
                temp_fermenter, 
                temp_fridge,
                cooler_active,
                heater_active,
                heartbeat_timestamp
            ) VALUES (?, ?, ?, ?, ?, ?, ?, NOW())
        ");
        
        $stmt->execute([
            $configId,
            $status,
            $uptimeSeconds,
            $tempFermenter,
            $tempFridge,
            $coolerActive,
            $heaterActive
        ]);
        
        echo json_encode([
            'success' => true,
            'message' => 'Heartbeat received',
            'heartbeat_id' => $pdo->lastInsertId()
        ]);
        
    } catch (PDOException $e) {
        error_log("Error inserting heartbeat: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['error' => 'Failed to save heartbeat']);
    }
    
} elseif ($_SERVER['REQUEST_METHOD'] === 'GET') {
    // Permite verificar último heartbeat
    $configId = $_GET['config_id'] ?? null;
    
    if (!$configId) {
        http_response_code(400);
        echo json_encode(['error' => 'config_id is required']);
        exit;
    }
    
    try {
        $stmt = $pdo->prepare("
            SELECT * FROM esp_heartbeat
            WHERE config_id = ?
            ORDER BY heartbeat_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $heartbeat = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($heartbeat) {
            // Calcula tempo desde último heartbeat
            $lastSeen = new DateTime($heartbeat['heartbeat_timestamp']);
            $now = new DateTime();
            $diff = $now->getTimestamp() - $lastSeen->getTimestamp();
            
            echo json_encode([
                'success' => true,
                'heartbeat' => $heartbeat,
                'seconds_since_last' => $diff,
                'is_online' => $diff < 120 // Online se < 2 minutos
            ]);
        } else {
            echo json_encode([
                'success' => false,
                'message' => 'No heartbeat found',
                'is_online' => false
            ]);
        }
        
    } catch (PDOException $e) {
        http_response_code(500);
        echo json_encode(['error' => 'Database error']);
    }
    
} else {
    http_response_code(405);
    echo json_encode(['error' => 'Method not allowed']);
}