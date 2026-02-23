<?php
// api/esp/state.php - COM LIMPEZA E CAMPOS DEDICADOS
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once $_SERVER['DOCUMENT_ROOT'] . '/classes/DatabaseCleanup.php';

$config_id = $_GET['config_id'] ?? null;
$input = json_decode(file_get_contents('php://input'), true);

if (!$config_id) {
    http_response_code(400);
    echo json_encode(['error' => 'config_id is required']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // ✅ CAPTURA OS VALORES DOS CAMPOS DEDICADOS
    // Tenta múltiplos nomes (comprimidos e não comprimidos)
    $stageStartEpoch = null;
    if (isset($input['stageStartEpoch'])) {
        $stageStartEpoch = (int)$input['stageStartEpoch'];
    } elseif (isset($input['stageStart'])) {
        $stageStartEpoch = (int)$input['stageStart'];
    } elseif (isset($input['sst'])) {
        $stageStartEpoch = (int)$input['sst'];
    }
    
    $targetReached = null;
    if (isset($input['targetReached'])) {
        $targetReached = $input['targetReached'] ? 1 : 0;
    } elseif (isset($input['tr'])) {
        // tr pode ser array [dias, horas, min, status] ou boolean
        if (is_bool($input['tr'])) {
            $targetReached = $input['tr'] ? 1 : 0;
        } elseif (is_array($input['tr'])) {
            $targetReached = 1;  // Se tem array de timeRemaining, alvo foi atingido
        }
    }
    
    // ✅ Log para debug (opcional - remover em produção se não precisar)
    error_log("ESP State - config_id: $config_id, stageStartEpoch: " . 
              ($stageStartEpoch ?? 'NULL') . ", targetReached: " . 
              ($targetReached ?? 'NULL'));
    
    // ✅ Salva estado COM os campos dedicados
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states (
            config_id, 
            state_data,
            stage_started_epoch,
            target_reached
        )
        VALUES (?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $config_id,
        json_encode($input),
        $stageStartEpoch,
        $targetReached
    ]);
    
    // ✅ LIMPEZA APÓS INSERT
    DatabaseCleanup::cleanupTable($pdo, 'fermentation_states', $config_id);
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    error_log("State error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['error' => 'Database error', 'message' => $e->getMessage()]);
}