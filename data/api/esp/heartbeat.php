<?php
// api/esp/heartbeat.php - Usar classe centralizada
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once $_SERVER['DOCUMENT_ROOT'] . '/classes/DatabaseCleanup.php'; // ✅ USAR CLASSE

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
        
        // ✅ USAR CLASSE CENTRALIZADA
        DatabaseCleanup::cleanupTable($pdo, 'esp_heartbeat', $configId);
        
        // ✅ LIMPEZA OCASIONAL DE ÓRFÃOS (5% de chance)
        if (rand(1, 20) === 1) {
            DatabaseCleanup::cleanupOrphans($pdo);
        }
                
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

function cleanupOldRecords($pdo, $tableName, $configId, $keepCount = 100, $timestampColumn = 'created_at') {
    try {
        $validTables = ['readings', 'controller_states', 'esp_heartbeat', 
                       'fermentation_states', 'ispindel_readings'];
        if (!in_array($tableName, $validTables)) {
            error_log("[CLEANUP ERROR] Tabela inválida: {$tableName}");
            return;
        }
        
        // 1. Limpeza do config_id específico
        if ($configId) {
            $sql = "SELECT COUNT(*) as total FROM `{$tableName}` WHERE config_id = ?";
            $stmt = $pdo->prepare($sql);
            $stmt->execute([$configId]);
            $count = $stmt->fetch(PDO::FETCH_ASSOC)['total'];
            
            // Ajuste: threshold mais preciso
            if ($count > $keepCount) {
                $sql = "
                    SELECT `{$timestampColumn}` as cutoff_time 
                    FROM `{$tableName}` 
                    WHERE config_id = ? 
                    ORDER BY `{$timestampColumn}` DESC 
                    LIMIT 1 OFFSET ?
                ";
                $stmt = $pdo->prepare($sql);
                $stmt->execute([$configId, $keepCount]);
                $result = $stmt->fetch(PDO::FETCH_ASSOC);
                
                if ($result && !empty($result['cutoff_time'])) {
                    // Ajuste: sem LIMIT para remover tudo de uma vez
                    $sql = "
                        DELETE FROM `{$tableName}` 
                        WHERE config_id = ? 
                        AND `{$timestampColumn}` < ?
                    ";
                    $stmt = $pdo->prepare($sql);
                    $stmt->execute([$configId, $result['cutoff_time']]);
                    
                    $deleted = $stmt->rowCount();
                    if ($deleted > 0) {
                        error_log("[CLEANUP] {$tableName}: config_id={$configId}, removidos {$deleted}");
                    }
                }
            }
        }
        
        // 2. Limpeza de órfãos (probabilística)
        if (rand(1, 20) === 1) {  // 5% de chance
            $stmt = $pdo->prepare("DELETE FROM `{$tableName}` WHERE config_id IS NULL LIMIT 500");
            $stmt->execute();
            $orphansDeleted = $stmt->rowCount();
            
            if ($orphansDeleted > 0) {
                error_log("[CLEANUP ORPHANS] {$tableName}: removidos {$orphansDeleted} órfãos");
            }
        }
        
    } catch (Exception $e) {
        error_log("[CLEANUP ERROR] {$tableName}: " . $e->getMessage());
    }
}
