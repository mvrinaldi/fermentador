<?php
// api/esp/stage.php - Atualiza etapa atual da fermentação
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

$input = json_decode(file_get_contents('php://input'), true);

$configId = $input['config_id'] ?? null;
$newStageIndex = $input['currentStageIndex'] ?? null;

if (!$configId || $newStageIndex === null) {
    http_response_code(400);
    echo json_encode([
        'success' => false, 
        'error' => 'config_id and currentStageIndex are required'
    ]);
    exit;
}

$configId = intval($configId);
$newStageIndex = intval($newStageIndex);

try {
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
        DB_USER, 
        DB_PASS
    );
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Inicia transação para garantir consistência
    $pdo->beginTransaction();
    
    // 1. Busca configuração atual
    $stmt = $pdo->prepare("
        SELECT id, current_stage_index, name, status
        FROM configurations
        WHERE id = ?
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $config = $stmt->fetch(PDO::FETCH_ASSOC);
    
    if (!$config) {
        $pdo->rollBack();
        http_response_code(404);
        echo json_encode(['success' => false, 'error' => 'Configuration not found']);
        exit;
    }
    
    $previousStageIndex = (int)$config['current_stage_index'];
    
    // 2. Marca etapa anterior como 'completed' na tabela stages
    $stmt = $pdo->prepare("
        UPDATE stages
        SET status = 'completed',
            end_time = NOW()
        WHERE config_id = ?
        AND stage_index = ?
        AND status IN ('running', 'waiting')
    ");
    $stmt->execute([$configId, $previousStageIndex]);
    
    // 3. Marca nova etapa como 'running'
    $stmt = $pdo->prepare("
        UPDATE stages
        SET status = 'running',
            start_time = NOW()
        WHERE config_id = ?
        AND stage_index = ?
        AND status = 'pending'
    ");
    $stmt->execute([$configId, $newStageIndex]);
    
    // 4. Busca temperatura alvo da nova etapa
    $stmt = $pdo->prepare("
        SELECT target_temp
        FROM stages
        WHERE config_id = ?
        AND stage_index = ?
        LIMIT 1
    ");
    $stmt->execute([$configId, $newStageIndex]);
    $newStage = $stmt->fetch(PDO::FETCH_ASSOC);
    $newTargetTemp = $newStage ? $newStage['target_temp'] : null;
    
    // 5. Atualiza configurations com novo índice e temperatura
    $stmt = $pdo->prepare("
        UPDATE configurations
        SET current_stage_index = ?,
            current_target_temp = ?,
            updated_at = NOW()
        WHERE id = ?
    ");
    $stmt->execute([$newStageIndex, $newTargetTemp, $configId]);
    
    // 6. Registra evento no histórico
    $stmt = $pdo->prepare("
        INSERT INTO action_history (config_id, action_type, action_details)
        VALUES (?, 'stage_advanced', ?)
    ");
    $stmt->execute([
        $configId,
        json_encode([
            'previous_stage' => $previousStageIndex,
            'new_stage' => $newStageIndex,
            'new_target_temp' => $newTargetTemp,
            'timestamp' => date('Y-m-d H:i:s'),
            'source' => 'esp8266'
        ])
    ]);
    
    // Confirma transação
    $pdo->commit();
    
    echo json_encode([
        'success' => true,
        'config_id' => $configId,
        'previous_stage_index' => $previousStageIndex,
        'current_stage_index' => $newStageIndex,
        'new_target_temp' => $newTargetTemp
    ]);
    
} catch (PDOException $e) {
    if ($pdo->inTransaction()) {
        $pdo->rollBack();
    }
    error_log("Stage update error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['success' => false, 'error' => 'Database error']);
}