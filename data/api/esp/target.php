<?php
// api/esp/target.php - Notifica que temperatura alvo foi atingida
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

$input = json_decode(file_get_contents('php://input'), true);

$configId = $input['config_id'] ?? null;
$targetReached = $input['target_reached'] ?? false;

if (!$configId || !$targetReached) {
    http_response_code(400);
    echo json_encode(['error' => 'Invalid data']);
    exit;
}

// Inicializar resposta
$response = ['success' => false, 'message' => ''];

try {
    // Conectar ao banco de dados
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Busca o stage atual
    $stmt = $pdo->prepare("
        SELECT current_stage_index
        FROM configurations
        WHERE id = ?
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $config = $stmt->fetch(PDO::FETCH_ASSOC);
    
    if (!$config) {
        http_response_code(404);
        echo json_encode(['error' => 'Configuration not found']);
        exit;
    }
    
    // Atualiza o stage para marcar que atingiu o alvo
    $stmt = $pdo->prepare("
        UPDATE stages
        SET target_reached_time = NOW()
        WHERE config_id = ?
        AND stage_index = ?
        AND target_reached_time IS NULL
    ");
    $stmt->execute([$configId, $config['current_stage_index']]);
    
    // Registra no histórico de ações
    $stmt = $pdo->prepare("
        INSERT INTO action_history (config_id, action_type, action_details)
        VALUES (?, 'target_reached', ?)
    ");
    $stmt->execute([
        $configId,
        json_encode(['stage_index' => $config['current_stage_index'], 'timestamp' => date('Y-m-d H:i:s')])
    ]);
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}