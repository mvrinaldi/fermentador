<?php
// api/esp/control.php
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

/**
 * Limpa registros antigos de um config_id específico E órfãos globais
 */
function cleanupOldRecords($pdo, $tableName, $configId, $keepCount = 100, $timestampColumn = 'created_at') {
    try {
        // ========== 1. LIMPEZA DO CONFIG_ID ESPECÍFICO ==========
        if ($configId) {
            $sql = "SELECT COUNT(*) as total FROM {$tableName} WHERE config_id = ?";
            $stmt = $pdo->prepare($sql);
            $stmt->execute([$configId]);
            $count = $stmt->fetch(PDO::FETCH_ASSOC)['total'];
            
            // Só limpa se tiver mais que 20% acima do limite
            if ($count > ($keepCount * 1.2)) {
                // Busca o timestamp de corte
                $sql = "
                    SELECT {$timestampColumn} as cutoff_time 
                    FROM {$tableName} 
                    WHERE config_id = ? 
                    ORDER BY {$timestampColumn} DESC 
                    LIMIT 1 OFFSET ?
                ";
                $stmt = $pdo->prepare($sql);
                $stmt->execute([$configId, $keepCount - 1]);
                $result = $stmt->fetch(PDO::FETCH_ASSOC);
                
                if ($result && isset($result['cutoff_time'])) {
                    $sql = "
                        DELETE FROM {$tableName} 
                        WHERE config_id = ? 
                        AND {$timestampColumn} < ?
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
        
        // ========== 2. LIMPEZA DE ÓRFÃOS (SEMPRE, AGRESSIVA) ==========
        // Deleta até 1000 órfãos por chamada
        $stmt = $pdo->prepare("DELETE FROM {$tableName} WHERE config_id IS NULL LIMIT 1000");
        $stmt->execute();
        $orphansDeleted = $stmt->rowCount();
        
        if ($orphansDeleted > 0) {
            error_log("[CLEANUP] {$tableName}: removidos {$orphansDeleted} órfãos");
        }
        
        // ========== 3. LIMPEZA GLOBAL (mantém apenas últimos X registros TOTAIS) ==========
        $globalLimit = $keepCount * 2; // Ex: se keepCount=100, mantém 200 no total
        
        $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM {$tableName}");
        $stmt->execute();
        $totalCount = $stmt->fetch(PDO::FETCH_ASSOC)['total'];
        
        if ($totalCount > $globalLimit * 1.2) {
            // Busca timestamp de corte global
            $sql = "
                SELECT {$timestampColumn} as cutoff_time 
                FROM {$tableName} 
                ORDER BY {$timestampColumn} DESC 
                LIMIT 1 OFFSET ?
            ";
            $stmt = $pdo->prepare($sql);
            $stmt->execute([$globalLimit - 1]);
            $result = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if ($result && isset($result['cutoff_time'])) {
                $sql = "DELETE FROM {$tableName} WHERE {$timestampColumn} < ? LIMIT 5000";
                $stmt = $pdo->prepare($sql);
                $stmt->execute([$result['cutoff_time']]);
                
                $deleted = $stmt->rowCount();
                if ($deleted > 0) {
                    error_log("[CLEANUP GLOBAL] {$tableName}: removidos {$deleted} registros antigos");
                }
            }
        }
        
    } catch (Exception $e) {
        error_log("[CLEANUP ERROR] {$tableName}: " . $e->getMessage());
    }
}

$input = json_decode(file_get_contents('php://input'), true);

error_log("control.php - Recebido: " . print_r($input, true));

$config_id = $input['config_id'] ?? null;
$setpoint = $input['setpoint'] ?? null;
$cooling = $input['cooling'] ?? false;
$heating = $input['heating'] ?? false;

// Se não há fermentação ativa, ignora
if (empty($config_id)) {
    echo json_encode([
        'success' => true,
        'message' => 'No active fermentation'
    ]);
    exit;
}

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    $stmt = $pdo->prepare("
        INSERT INTO controller_states (config_id, setpoint, cooling, heating)
        VALUES (?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $config_id,
        $setpoint,
        $cooling ? 1 : 0,
        $heating ? 1 : 0
    ]);
    
    // ✅ Limpa registros antigos E órfãos (mantém 200 por config_id)
    cleanupOldRecords($pdo, 'controller_states', $config_id, 200, 'state_timestamp');
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    error_log("Control error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode([
        'error' => 'Database error',
        'details' => $e->getMessage()
    ]);
}