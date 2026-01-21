<?php
// api/esp/active.php - Fermentação ativa
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

// Responde OPTIONS (preflight)
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Conexão banco de dados
require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

try {
    // Conectar ao banco de dados
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Busca fermentação ativa

    $stmt = $pdo->prepare("
        SELECT id, name, status, created_at, updated_at, current_stage_index
        FROM configurations
        WHERE status = 'active'
        ORDER BY started_at DESC
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
            'currentStageIndex' => (int)($config['current_stage_index'] ?? 0)
        ]);
    } else {
        // IMPORTANTE: Retorna false quando não há fermentação ativa
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