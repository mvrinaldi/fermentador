<?php
// api/esp/active.php - Fermentação ativa COM stageStartEpoch
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
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
    
    // ✅ Busca fermentação ativa + último estado COMPLETO
    $stmt = $pdo->prepare("
        SELECT 
            c.id, 
            c.name, 
            c.status, 
            c.created_at, 
            c.updated_at, 
            c.current_stage_index,
            fs.stage_started_epoch,
            fs.target_reached
        FROM configurations c
        LEFT JOIN fermentation_states fs ON fs.config_id = c.id
        WHERE c.status = 'active'
        ORDER BY c.started_at DESC, fs.state_timestamp DESC
        LIMIT 1
    ");
    $stmt->execute();
    $config = $stmt->fetch(PDO::FETCH_ASSOC);
    
    if ($config) {
        echo json_encode([
            'active' => true,
            'id' => (string)$config['id'],
            'name' => $config['name'],
            'status' => $config['status'],
            'currentStageIndex' => (int)($config['current_stage_index'] ?? 0),
            'stageStartEpoch' => $config['stage_started_epoch'] ? (int)$config['stage_started_epoch'] : 0,
            'targetReached' => $config['target_reached'] ? true : false
        ]);
    } else {
        echo json_encode([
            'active' => false,
            'id' => '',
            'message' => 'No active fermentation'
        ]);
    }
    
} catch (PDOException $e) {
    error_log("Active endpoint error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode([
        'error' => 'Database error',
        'message' => $e->getMessage()
    ]);
}