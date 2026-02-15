<?php
/**
 * API Unificada do Sistema de Fermentação
 * 
 * Inclui integração com Sistema de Alertas (WhatsApp/Telegram)
 * e DatabaseCleanup centralizado
 * 
 * @author Marcos Rinaldi
 * @version 2.2 - Correção integração alertas + limpeza código redundante
 * @date Fevereiro 2026
 */

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');
header('Content-Type: application/json');

date_default_timezone_set('UTC');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

session_start();

$session_timeout = 24 * 60 * 60;

if (isset($_SESSION['last_activity'])) {
    $elapsed = time() - $_SESSION['last_activity'];
    if ($elapsed > $session_timeout) {
        session_destroy();
        session_start();
    }
}

$_SESSION['last_activity'] = time();

// ==================== INCLUDES ====================

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once __DIR__ . '/classes/DatabaseCleanup.php';

// ✅ Sistema de Alertas - carregamento único e simplificado
$alertSystemAvailable = false;
try {
    $alertSystemFile = __DIR__ . '/classes/AlertSystem.php';
    $alertIntegrationFile = __DIR__ . '/api/AlertIntegration.php';
    
    if (file_exists($alertSystemFile) && file_exists($alertIntegrationFile)) {
        require_once $alertSystemFile;
        require_once $alertIntegrationFile;
        $alertSystemAvailable = true;
        error_log("[API] Sistema de alertas carregado com sucesso");
    } else {
        if (!file_exists($alertSystemFile)) {
            error_log("[API] AlertSystem.php não encontrado em: $alertSystemFile");
        }
        if (!file_exists($alertIntegrationFile)) {
            error_log("[API] AlertIntegration.php não encontrado em: $alertIntegrationFile");
        }
    }
} catch (Exception $e) {
    error_log("[API] Erro ao carregar sistema de alertas: " . $e->getMessage());
    $alertSystemAvailable = false;
}

// ==================== CONEXÃO COM BANCO ====================

try {
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
        DB_USER,
        DB_PASS
    );
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $pdo->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Erro de conexão com banco de dados']);
    exit;
}

// ==================== FUNÇÕES AUXILIARES ====================

/**
 * Descomprime dados de estado enviados pelo ESP (formato comprimido)
 */
function decompressStateData(&$data) {
    if (!is_array($data)) {
        return;
    }
    
    $messageMap = [
        'fc'    => 'Fermentação concluída automaticamente - mantendo temperatura',
        'fconc' => 'Fermentação concluída automaticamente - mantendo temperatura',
        'tc'    => 'Fermentação concluída',
        'fpaus' => 'Fermentação pausada',
        'ch'    => 'completed_holding_temp',
        'chold' => 'completed_holding_temp',
        'c'     => 'Resfriando',
        'cool'  => 'Resfriando',
        'h'     => 'Aquecendo',
        'heat'  => 'Aquecendo',
        'w'     => 'Aguardando',
        'wait'  => 'Aguardando',
        'i'     => 'Ocioso',
        'idle'  => 'Ocioso',
        'r'     => 'Executando',
        'run'   => 'Executando',
        'wg'    => 'waiting_gravity',
        'targ'  => 'Temperatura alvo atingida',
        'strt'  => 'Etapa iniciada',
        'ramp'  => 'Em rampa',
        'peak'  => 'Detectando pico',
        'err'   => 'Erro',
        'off'   => 'Desligado'
    ];
    
    $stageTypeMap = [
        't'  => 'temperature',
        'r'  => 'ramp',
        'g'  => 'gravity',
        'gt' => 'gravity_time'
    ];
    
    $unitMap = [
        'h' => 'hours',
        'd' => 'days',
        'm' => 'minutes',
        'ind' => 'indefinite',
        'tc' => 'completed'
    ];
    
    $statusMap = [
        'r'    => 'running',
        'run'  => 'running',
        'w'    => 'waiting',
        'wait' => 'waiting',
        'wg'   => 'waiting_gravity',
        'tc'   => 'completed'
    ];
    
    $fieldMap = [
        'cn'  => 'config_name',
        'csi' => 'currentStageIndex',
        'ts'  => 'totalStages',
        'stt' => 'stageTargetTemp',
        'ptt' => 'pidTargetTemp',
        'ctt' => 'currentTargetTemp',
        'c'   => 'cooling',
        'h'   => 'heating',
        's'   => 'status',
        'msg' => 'message',
        'cid' => 'config_id',
        'ca'  => 'completedAt',
        'tms' => 'timestamp',
        'um'  => 'uptime_ms',
        'rp'  => 'rampProgress',
        'st'  => 'stageType'
    ];
    
    // Expandir campos comprimidos
    foreach ($fieldMap as $short => $long) {
        if (array_key_exists($short, $data)) {
            $data[$long] = $data[$short];
            unset($data[$short]);
        }
    }
    
    // Processar timeRemaining (tr)
    if (isset($data['tr'])) {
        if (is_array($data['tr'])) {
            $tr = $data['tr'];
            
            if (count($tr) == 1 && $tr[0] === 'tc') {
                $data['timeRemaining'] = [
                    'value' => 0,
                    'unit' => 'completed',
                    'status' => 'completed',
                    'display' => 'Fermentação concluída'
                ];
                $data['targetReached'] = true;
                $data['fermentationCompleted'] = true;
            }
            elseif (count($tr) == 4 && is_numeric($tr[0]) && is_numeric($tr[1]) && is_numeric($tr[2])) {
                $data['timeRemaining'] = [
                    'days' => (int)$tr[0],
                    'hours' => (int)$tr[1],
                    'minutes' => (int)$tr[2],
                    'status' => isset($statusMap[$tr[3]]) ? $statusMap[$tr[3]] : 
                              (isset($messageMap[$tr[3]]) ? $messageMap[$tr[3]] : $tr[3]),
                    'unit' => 'detailed'
                ];
                $data['targetReached'] = true;
            }
            elseif (count($tr) == 3) {
                $data['timeRemaining'] = [
                    'value' => $tr[0],
                    'unit' => isset($unitMap[$tr[1]]) ? $unitMap[$tr[1]] : $tr[1],
                    'status' => isset($statusMap[$tr[2]]) ? $statusMap[$tr[2]] : 
                              (isset($messageMap[$tr[2]]) ? $messageMap[$tr[2]] : $tr[2])
                ];
                $data['targetReached'] = true;
            }
            
            unset($data['tr']);
            
        } elseif (is_bool($data['tr'])) {
            $data['targetReached'] = $data['tr'];
            unset($data['tr']);
        } elseif (is_string($data['tr']) && $data['tr'] === 'tc') {
            $data['timeRemaining'] = [
                'value' => 0,
                'unit' => 'completed',
                'status' => 'completed',
                'display' => 'Fermentação concluída'
            ];
            $data['targetReached'] = true;
            $data['fermentationCompleted'] = true;
            unset($data['tr']);
        }
    }
    
    // Expandir mensagens
    if (isset($data['message']) && is_string($data['message'])) {
        $msg = $data['message'];
        if (isset($messageMap[$msg])) {
            $data['message'] = $messageMap[$msg];
        }
    }
    
    // Expandir status
    if (isset($data['status']) && is_string($data['status'])) {
        $status = $data['status'];
        if (isset($messageMap[$status])) {
            $data['status'] = $messageMap[$status];
        } elseif (isset($statusMap[$status])) {
            $data['status'] = $statusMap[$status];
        }
    }
    
    // Expandir tipo de etapa
    if (isset($data['stageType']) && is_string($data['stageType']) && isset($stageTypeMap[$data['stageType']])) {
        $data['stageType'] = $stageTypeMap[$data['stageType']];
    }
    
    // Processar control_status
    if (isset($data['control_status']) && is_array($data['control_status'])) {
        $cs = &$data['control_status'];
        
        $csMap = [
            's'  => 'state',
            'iw' => 'is_waiting',
            'wr' => 'wait_reason',
            'ws' => 'wait_seconds',
            'wd' => 'wait_display',
            'pd' => 'peak_detection',
            'ep' => 'estimated_peak'
        ];
        
        foreach ($csMap as $short => $long) {
            if (isset($cs[$short])) {
                $cs[$long] = $cs[$short];
                unset($cs[$short]);
            }
        }
        
        if (isset($cs['state']) && is_string($cs['state']) && isset($messageMap[$cs['state']])) {
            $cs['state'] = $messageMap[$cs['state']];
        }
    }
}

/**
 * Verifica autenticação do usuário
 */
function requireAuth() {
    global $pdo;
    
    if (!isset($_SESSION['user_id'])) {
        http_response_code(401);
        echo json_encode(['error' => 'Não autenticado', 'require_login' => true]);
        exit;
    }
    
    $userId = (int)$_SESSION['user_id'];
    
    try {
        $stmt = $pdo->prepare("SELECT id, is_active FROM users WHERE id = ?");
        $stmt->execute([$userId]);
        $user = $stmt->fetch();
        
        if (!$user || !$user['is_active']) {
            session_destroy();
            http_response_code(401);
            echo json_encode(['error' => 'Usuário inválido', 'require_login' => true]);
            exit;
        }
        
        $_SESSION['last_activity'] = time();
        return $userId;
        
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['error' => 'Erro ao validar sessão']);
        exit;
    }
}

/**
 * Envia resposta JSON
 */
function sendResponse($data, $code = 200, $options = 0) {
    http_response_code($code);
    echo json_encode($data, $options);
    exit;
}

// ==================== FUNÇÕES DE ALERTA ====================

/**
 * Verifica alertas (chamado após heartbeat)
 */
function checkAlertsIfEnabled($pdo, $configId) {
    global $alertSystemAvailable;
    
    if (!$alertSystemAvailable || !$configId) {
        return;
    }
    
    try {
        AlertIntegration::checkAlertsOnHeartbeat($pdo, $configId);
    } catch (Exception $e) {
        error_log("[ALERTS] Erro ao verificar alertas: " . $e->getMessage());
    }
}

/**
 * ✅ Dispara alerta de etapa concluída
 */
function triggerStageCompletedAlert($pdo, $configId, $stageIndex, $stageName, $nextStageName = null) {
    global $alertSystemAvailable;
    
    error_log("[API] triggerStageCompletedAlert() chamado: available={$alertSystemAvailable}, configId={$configId}, stage={$stageIndex}");
    
    if (!$alertSystemAvailable) {
        error_log("[API] ❌ Sistema de alertas NÃO disponível!");
        return;
    }
    
    if (!$configId) {
        error_log("[API] ❌ configId vazio!");
        return;
    }
    
    try {
        AlertIntegration::onStageCompleted($pdo, $configId, $stageIndex, $stageName, $nextStageName);
        error_log("[API] ✅ AlertIntegration::onStageCompleted() executado com sucesso");
    } catch (Exception $e) {
        error_log("[API] ❌ Erro ao disparar alerta de etapa: " . $e->getMessage());
    }
}

/**
 * ✅ Dispara alerta de fermentação concluída
 */
function triggerFermentationCompletedAlert($pdo, $configId, $configName) {
    global $alertSystemAvailable;
    
    error_log("[API] triggerFermentationCompletedAlert() chamado: available={$alertSystemAvailable}, configId={$configId}");
    
    if (!$alertSystemAvailable) {
        error_log("[API] ❌ Sistema de alertas NÃO disponível!");
        return;
    }
    
    if (!$configId) {
        error_log("[API] ❌ configId vazio!");
        return;
    }
    
    try {
        AlertIntegration::onFermentationCompleted($pdo, $configId, $configName);
        error_log("[API] ✅ AlertIntegration::onFermentationCompleted() executado com sucesso");
    } catch (Exception $e) {
        error_log("[API] ❌ Erro ao disparar alerta de conclusão: " . $e->getMessage());
    }
}

/**
 * ✅ Dispara alerta de gravidade atingida
 */
function triggerGravityReachedAlert($pdo, $configId, $currentGravity, $targetGravity) {
    global $alertSystemAvailable;
    
    error_log("[API] triggerGravityReachedAlert() chamado: available={$alertSystemAvailable}, configId={$configId}");
    
    if (!$alertSystemAvailable) {
        return;
    }
    
    if (!$configId) {
        return;
    }
    
    try {
        AlertIntegration::onGravityReached($pdo, $configId, $currentGravity, $targetGravity);
    } catch (Exception $e) {
        error_log("[API] ❌ Erro ao disparar alerta de gravidade: " . $e->getMessage());
    }
}

// ==================== ROTEAMENTO ====================

$method = $_SERVER['REQUEST_METHOD'];
$path = isset($_GET['path']) ? $_GET['path'] : '';
$input = json_decode(file_get_contents('php://input'), true);

// ==================== AUTENTICAÇÃO ====================

if ($path === 'auth/login' && $method === 'POST') {
    $email = $input['email'] ?? '';
    $password = $input['password'] ?? '';
    
    if (empty($email) || empty($password)) {
        sendResponse(['error' => 'Email e senha são obrigatórios'], 400);
    }
    
    $stmt = $pdo->prepare("SELECT id, password_hash FROM users WHERE email = ? AND is_active = TRUE");
    $stmt->execute([$email]);
    $user = $stmt->fetch();
    
    if ($user && password_verify($password, $user['password_hash'])) {
        $_SESSION['user_id'] = (int)$user['id'];
        $_SESSION['user_email'] = $email;
        
        $stmt = $pdo->prepare("UPDATE users SET last_login = NOW() WHERE id = ?");
        $stmt->execute([$user['id']]);
        
        sendResponse([
            'success' => true,
            'user_id' => (int)$user['id']
        ], 200, JSON_FORCE_OBJECT);
    } else {
        sendResponse(['error' => 'Credenciais inválidas'], 401);
    }
}

if ($path === 'auth/logout' && $method === 'POST') {
    session_destroy();
    sendResponse(['success' => true]);
}

if ($path === 'auth/check' && $method === 'GET') {
    if (isset($_SESSION['user_id']) && is_numeric($_SESSION['user_id']) && $_SESSION['user_id'] > 0) {
        sendResponse([
            'authenticated' => true, 
            'user_id' => (int)$_SESSION['user_id']
        ]);
    } else {
        sendResponse(['authenticated' => false]);
    }
}

// ==================== CONFIGURAÇÕES ====================

if ($path === 'configurations' && $method === 'GET') {
    $userId = requireAuth();
    
    $stmt = $pdo->prepare("
        SELECT c.*, 
               (SELECT COUNT(*) FROM stages WHERE config_id = c.id) as stage_count
        FROM configurations c
        WHERE c.user_id = ?
        ORDER BY c.created_at DESC
    ");
    $stmt->execute([$userId]);
    $configs = $stmt->fetchAll();
    
    foreach ($configs as &$config) {
        $stmt = $pdo->prepare("SELECT * FROM stages WHERE config_id = ? ORDER BY stage_index");
        $stmt->execute([$config['id']]);
        $config['stages'] = $stmt->fetchAll();
    }
    
    sendResponse($configs);
}

if ($path === 'configurations' && $method === 'POST') {
    $userId = requireAuth();
    
    $name = $input['name'] ?? '';
    $stages = $input['stages'] ?? [];
    
    if (empty($name) || empty($stages)) {
        sendResponse(['error' => 'Nome e etapas são obrigatórios'], 400);
    }
    
    $pdo->beginTransaction();
    
    try {
        $stmt = $pdo->prepare("
            INSERT INTO configurations (user_id, name, status, current_stage_index, times_used)
            VALUES (?, ?, 'pending', 0, 0)
        ");
        $stmt->execute([$userId, $name]);
        $configId = $pdo->lastInsertId();
        
        foreach ($stages as $index => $stage) {
            $getValue = function($key, $default = null) use ($stage) {
                if (isset($stage[$key])) return $stage[$key];
                $camelKey = lcfirst(str_replace('_', '', ucwords($key, '_')));
                if (isset($stage[$camelKey])) return $stage[$camelKey];
                return $default;
            };
            
            $stmt = $pdo->prepare("
                INSERT INTO stages (
                    config_id, stage_index, type, target_temp, duration, 
                    target_gravity, max_duration, start_temp, ramp_time, 
                    actual_rate, direction, status
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending')
            ");
            
            $stmt->execute([
                $configId,
                $index,
                $getValue('type'),
                $getValue('target_temp'),
                $getValue('duration'),
                $getValue('target_gravity'),
                $getValue('max_duration'),
                $getValue('start_temp'),
                $getValue('ramp_time'),
                $getValue('actual_rate'),
                $getValue('direction')
            ]);
        }
        
        $pdo->commit();
        sendResponse(['success' => true, 'config_id' => $configId], 201);
        
    } catch (Exception $e) {
        $pdo->rollBack();
        sendResponse(['error' => 'Erro ao criar configuração: ' . $e->getMessage()], 500);
    }
}

if ($path === 'configurations/status' && $method === 'PUT') {
    $userId = requireAuth();
    
    $configId = $input['config_id'] ?? null;
    $status = $input['status'] ?? null;
    
    if (!$configId || !$status) {
        sendResponse(['error' => 'ID e status são obrigatórios'], 400);
    }
    
    $pdo->beginTransaction();
    
    try {
        $updateData = ['status' => $status];
        
        if ($status === 'active') {
            $updateData['started_at'] = date('Y-m-d H:i:s');
            $updateData['paused_at'] = null;
            $updateData['completed_at'] = null;
            $updateData['current_stage_index'] = 0;
            
            // Resetar etapas
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'pending',
                    start_time = NULL,
                    end_time = NULL,
                    target_reached_time = NULL
                WHERE config_id = ?
            ");
            $stmt->execute([$configId]);
            
            // Ativar primeira etapa
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'running', start_time = NOW() 
                WHERE config_id = ? AND stage_index = 0
            ");
            $stmt->execute([$configId]);
            
            // Incrementar contador de uso
            $stmt = $pdo->prepare("UPDATE configurations SET times_used = times_used + 1 WHERE id = ?");
            $stmt->execute([$configId]);
            
            // Limpar dados antigos (começar do zero)
            $stmt = $pdo->prepare("DELETE FROM readings WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM ispindel_readings WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM controller_states WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM fermentation_states WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM esp_heartbeat WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            // Limpar alertas antigos também
            $stmt = $pdo->prepare("DELETE FROM alerts WHERE config_id = ?");
            $stmt->execute([$configId]);
            
        } elseif ($status === 'paused') {
            $updateData['paused_at'] = date('Y-m-d H:i:s');
        } elseif ($status === 'completed') {
            $updateData['completed_at'] = date('Y-m-d H:i:s');
        }
        
        $setClauses = [];
        $values = [];
        foreach ($updateData as $key => $value) {
            $setClauses[] = "$key = ?";
            $values[] = $value;
        }
        $values[] = $configId;
        $values[] = $userId;
        
        $stmt = $pdo->prepare("
            UPDATE configurations 
            SET " . implode(', ', $setClauses) . "
            WHERE id = ? AND user_id = ?
        ");
        $stmt->execute($values);
        
        $pdo->commit();
        sendResponse(['success' => true]);
        
    } catch (Exception $e) {
        $pdo->rollBack();
        sendResponse(['error' => 'Erro ao atualizar status: ' . $e->getMessage()], 500);
    }
}

if ($path === 'configurations/delete' && $method === 'DELETE') {
    $userId = requireAuth();
    $configId = $input['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'ID é obrigatório'], 400);
    }
    
    $stmt = $pdo->prepare("SELECT status FROM configurations WHERE id = ? AND user_id = ?");
    $stmt->execute([$configId, $userId]);
    $config = $stmt->fetch();
    
    if (!$config) {
        sendResponse(['error' => 'Configuração não encontrada'], 404);
    }
    
    if ($config['status'] === 'active') {
        sendResponse(['error' => 'Não é possível excluir fermentação ativa'], 400);
    }
    
    $stmt = $pdo->prepare("DELETE FROM configurations WHERE id = ? AND user_id = ?");
    $stmt->execute([$configId, $userId]);
    
    sendResponse(['success' => true]);
}

// ==================== FERMENTAÇÃO ATIVA ====================

if ($path === 'active' && $method === 'GET') {
    $userId = requireAuth();
    
    $stmt = $pdo->prepare("
        SELECT c.id, c.name, c.current_stage_index, c.status
        FROM configurations c
        WHERE c.user_id = ? AND c.status = 'active'
        ORDER BY c.started_at DESC
        LIMIT 1
    ");
    $stmt->execute([$userId]);
    $active = $stmt->fetch();
    
    if ($active) {
        sendResponse([
            'active' => true,
            'id' => $active['id'],
            'name' => $active['name'],
            'currentStageIndex' => $active['current_stage_index']
        ]);
    } else {
        sendResponse(['active' => false, 'id' => null]);
    }
}

if ($path === 'active/activate' && $method === 'POST') {
    $userId = requireAuth();
    $configId = $input['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    $stmt = $pdo->prepare("SELECT id FROM configurations WHERE id = ? AND user_id = ?");
    $stmt->execute([$configId, $userId]);
    
    if (!$stmt->fetch()) {
        sendResponse(['error' => 'Configuração não encontrada'], 404);
    }
    
    sendResponse(['success' => true]);
}

if ($path === 'active/deactivate' && $method === 'POST') {
    requireAuth();
    sendResponse(['success' => true]);
}

// ==================== ÚLTIMAS LEITURAS (SEM FERMENTAÇÃO ATIVA) ====================

if ($path === 'latest-readings' && $method === 'GET') {
    requireAuth();
    
    $stmt = $pdo->prepare("
        SELECT temp_fridge, temp_fermenter, temp_target, reading_timestamp
        FROM readings 
        ORDER BY reading_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute();
    $reading = $stmt->fetch();
    
    $stmt = $pdo->prepare("
        SELECT * FROM ispindel_readings 
        ORDER BY reading_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute();
    $ispindel = $stmt->fetch();
    
    if ($ispindel && isset($ispindel['reading_timestamp'])) {
        $lastReading = new DateTime($ispindel['reading_timestamp']);
        $now = new DateTime();
        $diffSeconds = $now->getTimestamp() - $lastReading->getTimestamp();
        $ispindel['is_stale'] = $diffSeconds > 3600;
        $ispindel['seconds_since_update'] = $diffSeconds;
    }
    
    sendResponse([
        'reading' => $reading ?: null,
        'ispindel' => $ispindel ?: null,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

// ==================== ESTADO COMPLETO (ESP → FRONTEND) ====================

if ($path === 'state/complete' && $method === 'GET') {
    requireAuth();
    $configId = $_GET['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    // Busca configuração
    $stmt = $pdo->prepare("SELECT * FROM configurations WHERE id = ?");
    $stmt->execute([$configId]);
    $config = $stmt->fetch();
    
    if (!$config) {
        sendResponse(['error' => 'Configuração não encontrada'], 404);
    }
    
    // Busca etapas
    $stmt = $pdo->prepare("SELECT * FROM stages WHERE config_id = ? ORDER BY stage_index");
    $stmt->execute([$configId]);
    $stages = $stmt->fetchAll();
    
    // Busca estado da fermentação
    $stmt = $pdo->prepare("
        SELECT * FROM fermentation_states 
        WHERE config_id = ?
        ORDER BY state_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $state = $stmt->fetch();
    
    $stateData = [];
    if ($state && isset($state['state_data'])) {
        $stateData = json_decode($state['state_data'], true) ?? [];
    }
    
    // Busca leituras recentes (últimas 24h)
    $stmt = $pdo->prepare("
        SELECT id, config_id, temp_fridge, temp_fermenter, temp_target, reading_timestamp
        FROM readings 
        WHERE config_id = ? 
        AND reading_timestamp >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
        ORDER BY reading_timestamp ASC
    ");
    $stmt->execute([$configId]);
    $readings = $stmt->fetchAll();
    
    // Busca última leitura do iSpindel
    $stmt = $pdo->prepare("
        SELECT * FROM ispindel_readings 
        WHERE config_id = ?
        ORDER BY reading_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $ispindel = $stmt->fetch();
    
    if ($ispindel && isset($ispindel['reading_timestamp'])) {
        $lastReading = new DateTime($ispindel['reading_timestamp']);
        $now = new DateTime();
        $diffSeconds = $now->getTimestamp() - $lastReading->getTimestamp();
        $ispindel['is_stale'] = $diffSeconds > 3600;
        $ispindel['seconds_since_update'] = $diffSeconds;
    }
    
    // Busca leituras do iSpindel das últimas 24h
    $stmt = $pdo->prepare("
        SELECT id, config_id, temperature, gravity, battery, reading_timestamp
        FROM ispindel_readings 
        WHERE config_id = ?
        AND reading_timestamp >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
        ORDER BY reading_timestamp ASC
    ");
    $stmt->execute([$configId]);
    $ispindelReadings = $stmt->fetchAll();
    
    // Busca histórico de estados do controlador
    $stmt = $pdo->prepare("
        SELECT * FROM controller_states 
        WHERE config_id = ?
        AND state_timestamp >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
        ORDER BY state_timestamp ASC
    ");
    $stmt->execute([$configId]);
    $controllerHistory = $stmt->fetchAll();
    
    // Busca estado atual do controlador
    $stmt = $pdo->prepare("
        SELECT * FROM controller_states 
        WHERE config_id = ?
        ORDER BY state_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $controller = $stmt->fetch();
    
    // Busca último heartbeat
    $heartbeat = null;
    $isOnline = false;
    try {
        $stmt = $pdo->prepare("
            SELECT * FROM esp_heartbeat
            WHERE config_id = ?
            ORDER BY heartbeat_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $heartbeat = $stmt->fetch();
        
        if ($heartbeat) {
            $lastSeen = new DateTime($heartbeat['heartbeat_timestamp']);
            $now = new DateTime();
            $diff = $now->getTimestamp() - $lastSeen->getTimestamp();
            $isOnline = $diff < 120;
            
            if (isset($heartbeat['control_status']) && is_string($heartbeat['control_status'])) {
                $heartbeat['control_status'] = json_decode($heartbeat['control_status'], true);
            }
        }
    } catch (PDOException $e) {
        error_log("Error fetching heartbeat: " . $e->getMessage());
    }
    
    // Busca alertas não lidos
    $unreadAlerts = 0;
    try {
        $stmt = $pdo->prepare("SELECT COUNT(*) as count FROM alerts WHERE config_id = ? AND is_read = 0");
        $stmt->execute([$configId]);
        $unreadAlerts = $stmt->fetch()['count'] ?? 0;
    } catch (Exception $e) {
        // Tabela pode não existir
    }
    
    sendResponse([
        'config' => array_merge($config, ['stages' => $stages]),
        'state' => $stateData,
        'readings' => $readings,
        'ispindel' => $ispindel ?: null,
        'ispindel_readings' => $ispindelReadings ?: [],
        'controller' => $controller ?: null,
        'controller_history' => $controllerHistory ?: [],
        'heartbeat' => $heartbeat ?: null,
        'is_online' => $isOnline,
        'unread_alerts' => $unreadAlerts,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

// ==================== LEITURAS ====================
if ($path === 'readings' && $method === 'POST') {
    // Aceita tanto formato comprimido quanto expandido
    $configId = $input['config_id'] ?? $input['cid'] ?? null;
    $tempFridge = $input['temp_fridge'] ?? $input['tf'] ?? null;
    $tempFermenter = $input['temp_fermenter'] ?? $input['tb'] ?? null;
    $tempTarget = $input['temp_target'] ?? $input['tt'] ?? null;
    
    // Validação básica
    if ($tempFridge === null || $tempFermenter === null || $tempTarget === null) {
        sendResponse(['error' => 'Dados incompletos: temperaturas obrigatórias'], 400);
    }
    
    try {
        // Se não tem config_id, busca fermentação ativa
        if (!$configId) {
            $stmt = $pdo->prepare("
                SELECT id FROM configurations 
                WHERE status = 'active' 
                ORDER BY started_at DESC 
                LIMIT 1
            ");
            $stmt->execute();
            $activeConfig = $stmt->fetch();
            
            if ($activeConfig) {
                $configId = $activeConfig['id'];
            } else {
                // Sem fermentação ativa - AINDA ASSIM salva a leitura (config_id = null)
                error_log("[API] Salvando leitura sem fermentação ativa");
            }
        }
        
        // ✅ INSERT DA LEITURA
        $stmt = $pdo->prepare("
            INSERT INTO readings (config_id, temp_fridge, temp_fermenter, temp_target)
            VALUES (?, ?, ?, ?)
        ");
        
        $stmt->execute([$configId, $tempFridge, $tempFermenter, $tempTarget]);
        $readingId = $pdo->lastInsertId();
        
        // Limpeza automática se tiver config_id
        if ($configId) {
            DatabaseCleanup::cleanupTable($pdo, 'readings', $configId);
        }
        
        // Log de sucesso
        error_log(sprintf(
            "[API] ✅ Leitura salva: ID=%d, Config=%s, Ferm=%.1f, Fridge=%.1f, Target=%.1f",
            $readingId,
            $configId ?? 'null',
            $tempFermenter,
            $tempFridge,
            $tempTarget
        ));
        
        sendResponse([
            'success' => true, 
            'reading_id' => $readingId,
            'config_id' => $configId
        ], 201);
        
    } catch (Exception $e) {
        error_log("[API] ❌ Erro ao salvar leitura: " . $e->getMessage());
        sendResponse(['error' => 'Erro ao salvar leitura: ' . $e->getMessage()], 500);
    }
}

// ==================== ISPINDEL ====================

if ($path === 'ispindel/data' && $method === 'POST') {
    $name = $input['name'] ?? 'iSpindel';
    $temperature = $input['temperature'] ?? null;
    $gravity = $input['gravity'] ?? null;
    $battery = $input['battery'] ?? null;
    $configIdFromEsp = $input['config_id'] ?? null;
    
    if ($temperature === null || $gravity === null) {
        sendResponse(['error' => 'temperature and gravity are required'], 400);
    }
    
    try {
        $configId = null;
        
        if ($configIdFromEsp) {
            $stmt = $pdo->prepare("SELECT id FROM configurations WHERE id = ? AND status = 'active'");
            $stmt->execute([$configIdFromEsp]);
            $validConfig = $stmt->fetch();
            
            if ($validConfig) {
                $configId = $configIdFromEsp;
            }
        }
        
        if (!$configId) {
            $stmt = $pdo->prepare("
                SELECT c.id FROM configurations c
                WHERE c.status = 'active'
                LIMIT 1
            ");
            $stmt->execute();
            $activeConfig = $stmt->fetch();
            $configId = $activeConfig ? $activeConfig['id'] : null;
        }
        
        $stmt = $pdo->prepare("
            INSERT INTO ispindel_readings (config_id, name, temperature, gravity, battery)
            VALUES (?, ?, ?, ?, ?)
        ");
        $stmt->execute([$configId, $name, $temperature, $gravity, $battery]);
        
        if ($configId) {
            DatabaseCleanup::cleanupTable($pdo, 'ispindel_readings', $configId);
        }
        
        sendResponse([
            'success' => true, 
            'message' => 'iSpindel data saved',
            'config_id' => $configId
        ], 201);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Error saving iSpindel data: ' . $e->getMessage()], 500);
    }
}

// ==================== CONTROLE ====================

if ($path === 'control' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    $setpoint = $input['setpoint'] ?? null;
    $cooling = $input['cooling'] ?? false;
    $heating = $input['heating'] ?? false;
    
    if (!$configId || $setpoint === null) {
        sendResponse(['error' => 'Dados incompletos'], 400);
    }
    
    $stmt = $pdo->prepare("
        INSERT INTO controller_states (config_id, setpoint, cooling, heating)
        VALUES (?, ?, ?, ?)
    ");
    $stmt->execute([$configId, $setpoint, $cooling, $heating]);
    
    DatabaseCleanup::cleanupTable($pdo, 'controller_states', $configId);
    
    sendResponse(['success' => true], 201);
}

// ==================== ESTADO FERMENTAÇÃO ====================

if ($path === 'fermentation-state' && $method === 'POST') {
    $configId = $input['config_id'] ?? $input['cid'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    decompressStateData($input);
    
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states (config_id, state_data)
        VALUES (?, ?)
    ");
    $stmt->execute([$configId, json_encode($input)]);
    
    DatabaseCleanup::cleanupTable($pdo, 'fermentation_states', $configId);
    
    sendResponse(['success' => true], 201);
}

// ==================== HEARTBEAT ====================

if ($path === 'heartbeat' && $method === 'POST') {
    $configId = $input['config_id'] ?? $input['cid'] ?? null;
    $uptime = $input['uptime'] ?? $input['uptime_seconds'] ?? null;
    $freeHeap = $input['free_heap'] ?? null;
    $controlStatus = $input['control_status'] ?? null;
    $tempFermenter = $input['temp_fermenter'] ?? null;
    $tempFridge = $input['temp_fridge'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    try {
        if ($controlStatus && is_array($controlStatus)) {
            decompressStateData($controlStatus);
        }
        
        $stmt = $pdo->prepare("
            INSERT INTO esp_heartbeat (
                config_id, 
                uptime_seconds, 
                free_heap, 
                temp_fermenter,
                temp_fridge,
                control_status
            )
            VALUES (?, ?, ?, ?, ?, ?)
        ");
        $stmt->execute([
            $configId, 
            $uptime, 
            $freeHeap,
            $tempFermenter,
            $tempFridge,
            $controlStatus ? json_encode($controlStatus) : null
        ]);
        
        DatabaseCleanup::cleanupTable($pdo, 'esp_heartbeat', $configId);
        
        // Limpeza ocasional de órfãos (5% de chance)
        if (rand(1, 20) === 1) {
            DatabaseCleanup::cleanupOrphans($pdo);
        }
        
        // ✅ Verificar alertas após heartbeat
        checkAlertsIfEnabled($pdo, $configId);
        
        sendResponse(['success' => true], 201);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Error saving heartbeat: ' . $e->getMessage()], 500);
    }
}

// ==================== NOTIFICAÇÃO DE ETAPA (do ESP) ====================

if ($path === 'stage/advance' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    $newStageIndex = $input['stage_index'] ?? $input['currentStageIndex'] ?? null;
    
    if (!$configId || $newStageIndex === null) {
        sendResponse(['error' => 'config_id e stage_index são obrigatórios'], 400);
    }
    
    error_log("[API] ========================================");
    error_log("[API] stage/advance RECEBIDO: config_id={$configId}, new_stage={$newStageIndex}");
    error_log("[API] alertSystemAvailable=" . ($alertSystemAvailable ? 'true' : 'false'));
    error_log("[API] ========================================");
    
    $pdo->beginTransaction();
    
    try {
        // Buscar informações da configuração e etapas
        $stmt = $pdo->prepare("SELECT name, current_stage_index FROM configurations WHERE id = ?");
        $stmt->execute([$configId]);
        $config = $stmt->fetch();
        
        if (!$config) {
            throw new Exception('Configuração não encontrada');
        }
        
        $previousIndex = $config['current_stage_index'];
        $configName = $config['name'];
        
        // Buscar total de etapas
        $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM stages WHERE config_id = ?");
        $stmt->execute([$configId]);
        $totalStages = $stmt->fetch()['total'];
        
        // Marcar etapa anterior como concluída
        $stmt = $pdo->prepare("
            UPDATE stages 
            SET status = 'completed', end_time = NOW() 
            WHERE config_id = ? AND stage_index = ?
        ");
        $stmt->execute([$configId, $previousIndex]);
        
        // Buscar nome da etapa concluída
        $stmt = $pdo->prepare("SELECT type, target_temp FROM stages WHERE config_id = ? AND stage_index = ?");
        $stmt->execute([$configId, $previousIndex]);
        $completedStage = $stmt->fetch();
        $completedStageName = "Etapa " . ($previousIndex + 1) . " (" . ($completedStage['type'] ?? 'unknown') . ")";
        
        // Registrar no action_history
        $stmt = $pdo->prepare("
            INSERT INTO action_history (config_id, action_type, action_details)
            VALUES (?, 'stage_advanced', ?)
        ");
        $stmt->execute([$configId, json_encode([
            'previous_stage' => $previousIndex,
            'new_stage' => $newStageIndex,
            'new_target_temp' => null,
            'timestamp' => date('Y-m-d H:i:s'),
            'source' => 'esp8266'
        ])]);
        
        // Verificar se é a última etapa
        if ($newStageIndex >= $totalStages) {
            // Fermentação concluída
            $stmt = $pdo->prepare("
                UPDATE configurations 
                SET status = 'completed', completed_at = NOW() 
                WHERE id = ?
            ");
            $stmt->execute([$configId]);
            
            $pdo->commit();
            
            // ✅ Disparar alerta de fermentação concluída (APÓS commit)
            error_log("[API] Fermentação concluída! Disparando alerta...");
            triggerFermentationCompletedAlert($pdo, $configId, $configName);
            
            sendResponse([
                'success' => true,
                'fermentation_completed' => true,
                'message' => 'Fermentação concluída!'
            ]);
        } else {
            // Ativar nova etapa
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'running', start_time = NOW() 
                WHERE config_id = ? AND stage_index = ?
            ");
            $stmt->execute([$configId, $newStageIndex]);
            
            // Atualizar índice na configuração
            $stmt = $pdo->prepare("
                UPDATE configurations 
                SET current_stage_index = ? 
                WHERE id = ?
            ");
            $stmt->execute([$newStageIndex, $configId]);
            
            // Buscar nova temperatura alvo
            $stmt = $pdo->prepare("SELECT type, target_temp FROM stages WHERE config_id = ? AND stage_index = ?");
            $stmt->execute([$configId, $newStageIndex]);
            $newStage = $stmt->fetch();
            $newStageName = "Etapa " . ($newStageIndex + 1) . " (" . ($newStage['type'] ?? 'unknown') . ")";
            $newTargetTemp = $newStage['target_temp'] ?? null;
            
            // Atualizar temperatura alvo na configuração
            if ($newTargetTemp !== null) {
                $stmt = $pdo->prepare("UPDATE configurations SET current_target_temp = ? WHERE id = ?");
                $stmt->execute([$newTargetTemp, $configId]);
            }
            
            // Atualizar action_history com target_temp
            $stmt = $pdo->prepare("
                UPDATE action_history 
                SET action_details = ? 
                WHERE config_id = ? AND action_type = 'stage_advanced'
                ORDER BY action_timestamp DESC LIMIT 1
            ");
            $stmt->execute([json_encode([
                'previous_stage' => $previousIndex,
                'new_stage' => $newStageIndex,
                'new_target_temp' => $newTargetTemp,
                'timestamp' => date('Y-m-d H:i:s'),
                'source' => 'esp8266'
            ]), $configId]);
            
            $pdo->commit();
            
            // ✅ Disparar alerta de etapa concluída (APÓS commit)
            error_log("[API] Etapa avançou: {$completedStageName} -> {$newStageName}. Disparando alerta...");
            triggerStageCompletedAlert($pdo, $configId, $previousIndex, $completedStageName, $newStageName);
            
            sendResponse([
                'success' => true,
                'previous_stage_index' => $previousIndex,
                'current_stage_index' => $newStageIndex,
                'new_target_temp' => $newTargetTemp
            ]);
        }
        
    } catch (Exception $e) {
        $pdo->rollBack();
        error_log("[API] ❌ Erro em stage/advance: " . $e->getMessage());
        sendResponse(['error' => 'Erro ao avançar etapa: ' . $e->getMessage()], 500);
    }
}

// ==================== NOTIFICAÇÃO DE ALVO ATINGIDO (do ESP) ====================

if ($path === 'target/reached' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    $stageIndex = $input['stage_index'] ?? null;
    $targetType = $input['target_type'] ?? 'temperature';
    $currentValue = $input['current_value'] ?? null;
    $targetValue = $input['target_value'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    try {
        // Atualizar target_reached_time na etapa
        $stmt = $pdo->prepare("
            UPDATE stages 
            SET target_reached_time = NOW() 
            WHERE config_id = ? AND stage_index = ?
        ");
        $stmt->execute([$configId, $stageIndex ?? 0]);
        
        // Registrar no action_history
        $stmt = $pdo->prepare("
            INSERT INTO action_history (config_id, action_type, action_details)
            VALUES (?, 'target_reached', ?)
        ");
        $stmt->execute([$configId, json_encode([
            'target_type' => $targetType,
            'current_value' => $currentValue,
            'target_value' => $targetValue,
            'stage_index' => $stageIndex
        ])]);
        
        // ✅ Se for gravidade, disparar alerta específico
        if ($targetType === 'gravity' && $currentValue !== null && $targetValue !== null) {
            triggerGravityReachedAlert($pdo, $configId, $currentValue, $targetValue);
        }
        
        sendResponse(['success' => true], 201);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Erro: ' . $e->getMessage()], 500);
    }
}

// ==================== ALERTAS (API SIMPLIFICADA) ====================

if ($path === 'alerts' && $method === 'GET') {
    requireAuth();
    $configId = $_GET['config_id'] ?? null;
    
    try {
        $sql = "SELECT * FROM alerts WHERE is_read = 0";
        $params = [];
        
        if ($configId) {
            $sql .= " AND config_id = ?";
            $params[] = $configId;
        }
        
        $sql .= " ORDER BY created_at DESC LIMIT 50";
        
        $stmt = $pdo->prepare($sql);
        $stmt->execute($params);
        $alerts = $stmt->fetchAll();
        
        sendResponse([
            'success' => true,
            'count' => count($alerts),
            'alerts' => $alerts
        ]);
    } catch (Exception $e) {
        sendResponse(['error' => $e->getMessage()], 500);
    }
}

if ($path === 'alerts/read' && $method === 'POST') {
    requireAuth();
    $alertId = $input['alert_id'] ?? null;
    
    if (!$alertId) {
        sendResponse(['error' => 'alert_id é obrigatório'], 400);
    }
    
    try {
        $stmt = $pdo->prepare("UPDATE alerts SET is_read = 1 WHERE id = ?");
        $stmt->execute([$alertId]);
        sendResponse(['success' => true]);
    } catch (Exception $e) {
        sendResponse(['error' => $e->getMessage()], 500);
    }
}

if ($path === 'alerts/read-all' && $method === 'POST') {
    requireAuth();
    $configId = $input['config_id'] ?? null;
    
    try {
        $sql = "UPDATE alerts SET is_read = 1 WHERE is_read = 0";
        $params = [];
        
        if ($configId) {
            $sql .= " AND config_id = ?";
            $params[] = $configId;
        }
        
        $stmt = $pdo->prepare($sql);
        $stmt->execute($params);
        sendResponse(['success' => true, 'updated' => $stmt->rowCount()]);
    } catch (Exception $e) {
        sendResponse(['error' => $e->getMessage()], 500);
    }
}

// ==================== LIMPEZA MANUAL ====================

if ($path === 'cleanup' && $method === 'POST') {
    $userId = requireAuth();
    
    try {
        $stmt = $pdo->prepare("SELECT id FROM configurations WHERE user_id = ?");
        $stmt->execute([$userId]);
        $configs = $stmt->fetchAll();
        
        $cleaned = 0;
        foreach ($configs as $config) {
            $configId = $config['id'];
            
            DatabaseCleanup::cleanupTable($pdo, 'readings', $configId);
            DatabaseCleanup::cleanupTable($pdo, 'controller_states', $configId);
            DatabaseCleanup::cleanupTable($pdo, 'fermentation_states', $configId);
            DatabaseCleanup::cleanupTable($pdo, 'esp_heartbeat', $configId);
            DatabaseCleanup::cleanupTable($pdo, 'ispindel_readings', $configId);
            
            $cleaned++;
        }
        
        sendResponse([
            'success' => true, 
            'message' => "Limpeza executada em {$cleaned} configurações"
        ]);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Erro na limpeza: ' . $e->getMessage()], 500);
    }
}

// ==================== LIMPEZA EMERGENCIAL ====================

if ($path === 'emergency-cleanup' && $method === 'POST') {
    $secretKey = $input['secret'] ?? '';
    
    if ($secretKey !== 'ferment2024cleanup') {
        sendResponse(['error' => 'Chave inválida'], 403);
    }
    
    try {
        $results = [];
        
        $stmt = $pdo->prepare("SELECT DISTINCT id FROM configurations WHERE status = 'active'");
        $stmt->execute();
        $configs = $stmt->fetchAll(PDO::FETCH_COLUMN);
        
        foreach ($configs as $configId) {
            $results["config_{$configId}"] = [
                'readings' => DatabaseCleanup::aggressiveCleanup($pdo, 'readings', $configId, 200),
                'controller_states' => DatabaseCleanup::aggressiveCleanup($pdo, 'controller_states', $configId, 200),
                'esp_heartbeat' => DatabaseCleanup::aggressiveCleanup($pdo, 'esp_heartbeat', $configId, 50),
                'fermentation_states' => DatabaseCleanup::aggressiveCleanup($pdo, 'fermentation_states', $configId, 100),
                'ispindel_readings' => DatabaseCleanup::aggressiveCleanup($pdo, 'ispindel_readings', $configId, 500)
            ];
        }
        
        $results['orphans_cleaned'] = DatabaseCleanup::cleanupOrphans($pdo);
        
        sendResponse([
            'success' => true,
            'results' => $results,
            'timestamp' => date('Y-m-d H:i:s')
        ]);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Erro na limpeza emergencial: ' . $e->getMessage()], 500);
    }
}

// ==================== ROTA NÃO ENCONTRADA ====================

http_response_code(404);
echo json_encode(['error' => 'Rota não encontrada: ' . $path]);