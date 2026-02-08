<?php
/**
 * api/esp/stage.php - Atualiza etapa atual da fermentação
 * 
 * v1.1 - Com integração ao Sistema de Alertas
 * 
 * @author Marcos Rinaldi
 * @date Fevereiro 2026
 */

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

// Carregar sistema de alertas
$alertSystemAvailable = false;
try {
    $alertSystemFile = $_SERVER['DOCUMENT_ROOT'] . '/fermentador/classes/AlertSystem.php';
    $alertIntegrationFile = $_SERVER['DOCUMENT_ROOT'] . '/fermentador/api/AlertIntegration.php';
    
    // Fallback: tentar caminhos relativos
    if (!file_exists($alertSystemFile)) {
        $alertSystemFile = dirname(dirname(__DIR__)) . '/classes/AlertSystem.php';
    }
    if (!file_exists($alertIntegrationFile)) {
        $alertIntegrationFile = dirname(__DIR__) . '/AlertIntegration.php';
    }
    
    if (file_exists($alertSystemFile) && file_exists($alertIntegrationFile)) {
        require_once $alertSystemFile;
        require_once $alertIntegrationFile;
        $alertSystemAvailable = true;
        error_log("[STAGE] Sistema de alertas carregado com sucesso");
    } else {
        error_log("[STAGE] Arquivos de alerta não encontrados:");
        error_log("[STAGE]   AlertSystem: $alertSystemFile (" . (file_exists($alertSystemFile) ? 'OK' : 'NÃO ENCONTRADO') . ")");
        error_log("[STAGE]   AlertIntegration: $alertIntegrationFile (" . (file_exists($alertIntegrationFile) ? 'OK' : 'NÃO ENCONTRADO') . ")");
    }
} catch (Exception $e) {
    error_log("[STAGE] Erro ao carregar alertas: " . $e->getMessage());
}

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
    $configName = $config['name'];
    
    // 2. Busca info da etapa concluída (para o alerta)
    $stmt = $pdo->prepare("SELECT type, target_temp FROM stages WHERE config_id = ? AND stage_index = ?");
    $stmt->execute([$configId, $previousStageIndex]);
    $completedStage = $stmt->fetch(PDO::FETCH_ASSOC);
    $completedStageName = "Etapa " . ($previousStageIndex + 1) . " (" . ($completedStage['type'] ?? 'unknown') . ")";
    
    // 3. Busca total de etapas
    $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM stages WHERE config_id = ?");
    $stmt->execute([$configId]);
    $totalStages = (int)$stmt->fetch(PDO::FETCH_ASSOC)['total'];
    
    // 4. Marca etapa anterior como 'completed'
    $stmt = $pdo->prepare("
        UPDATE stages
        SET status = 'completed',
            end_time = NOW()
        WHERE config_id = ?
        AND stage_index = ?
        AND status IN ('running', 'waiting')
    ");
    $stmt->execute([$configId, $previousStageIndex]);
    
    // 5. Verifica se é a última etapa
    $isFermentationComplete = ($newStageIndex >= $totalStages);
    
    $newTargetTemp = null;
    $newStageName = null;
    
    if (!$isFermentationComplete) {
        // 6a. Marca nova etapa como 'running'
        $stmt = $pdo->prepare("
            UPDATE stages
            SET status = 'running',
                start_time = NOW()
            WHERE config_id = ?
            AND stage_index = ?
            AND status = 'pending'
        ");
        $stmt->execute([$configId, $newStageIndex]);
        
        // 7a. Busca temperatura alvo da nova etapa
        $stmt = $pdo->prepare("SELECT type, target_temp FROM stages WHERE config_id = ? AND stage_index = ?");
        $stmt->execute([$configId, $newStageIndex]);
        $newStage = $stmt->fetch(PDO::FETCH_ASSOC);
        $newTargetTemp = $newStage ? $newStage['target_temp'] : null;
        $newStageName = "Etapa " . ($newStageIndex + 1) . " (" . ($newStage['type'] ?? 'unknown') . ")";
        
        // 8a. Atualiza configurations
        $stmt = $pdo->prepare("
            UPDATE configurations
            SET current_stage_index = ?,
                current_target_temp = ?,
                updated_at = NOW()
            WHERE id = ?
        ");
        $stmt->execute([$newStageIndex, $newTargetTemp, $configId]);
    } else {
        // 6b. Fermentação concluída
        $stmt = $pdo->prepare("
            UPDATE configurations
            SET status = 'completed',
                completed_at = NOW(),
                updated_at = NOW()
            WHERE id = ?
        ");
        $stmt->execute([$configId]);
    }
    
    // 9. Registra evento no histórico
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
    
    // DISPARAR ALERTAS (após commit, para não bloquear a transação)
    if ($alertSystemAvailable) {
        try {
            if ($isFermentationComplete) {
                error_log("[STAGE] Fermentação concluída! Disparando alerta...");
                AlertIntegration::onFermentationCompleted($pdo, $configId, $configName);
            } else {
                error_log("[STAGE] Etapa avançou: {$completedStageName} -> {$newStageName}. Disparando alerta...");
                AlertIntegration::onStageCompleted($pdo, $configId, $previousStageIndex, $completedStageName, $newStageName);
            }
        } catch (Exception $e) {
            // Não deixa erro de alerta afetar a resposta ao ESP
            error_log("[STAGE] Erro ao disparar alerta: " . $e->getMessage());
        }
    } else {
        error_log("[STAGE] Sistema de alertas não disponível - alerta NÃO disparado");
    }
    
    // Resposta ao ESP
    $response = [
        'success' => true,
        'config_id' => $configId,
        'previous_stage_index' => $previousStageIndex,
        'current_stage_index' => $newStageIndex,
        'new_target_temp' => $newTargetTemp
    ];
    
    if ($isFermentationComplete) {
        $response['fermentation_completed'] = true;
    }
    
    echo json_encode($response);
    
} catch (PDOException $e) {
    if (isset($pdo) && $pdo->inTransaction()) {
        $pdo->rollBack();
    }
    error_log("[STAGE] Database error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['success' => false, 'error' => 'Database error']);
}