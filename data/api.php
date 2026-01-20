<?php
// api.php
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

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $pdo->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Erro de conexão com banco de dados']);
    exit;
}

// ==================== FUNÇÕES AUXILIARES ====================

function decompressStateData(&$data) {
    if (!is_array($data)) {
        return;
    }
    
    // ========== MAPEAMENTOS COMPATÍVEIS COM ESP32 ==========
    $messageMap = [
        // Mensagens gerais
        'fconc' => 'Fermentação concluída automaticamente - mantendo temperatura',
        'fcomp' => 'Fermentação concluída',
        'fpaus' => 'Fermentação pausada',
        'chold' => 'completed_holding_temp',
        
        // Estados do controle
        'cool'  => 'Resfriando',
        'heat'  => 'Aquecendo',
        'wait'  => 'Aguardando',
        'idle'  => 'Ocioso',
        'run'   => 'Executando',
        'wg'    => 'waiting_gravity',
        
        // Adicionais para compatibilidade
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
        'ind' => 'indefinite'
    ];
    
    $statusMap = [
        'run' => 'running',
        'wait' => 'waiting',
        'wg' => 'waiting_gravity'
    ];
    
    error_log("DEBUG decompressStateData INPUT: " . json_encode($data));
    
    // ========== 1. PRIMEIRO: Expandir TODOS os campos abreviados ==========
    $fieldMap = [
        // Campos principais
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
        // NOTA: 'tr' NÃO está aqui porque precisa de processamento especial
    ];
    
    foreach ($fieldMap as $short => $long) {
        if (array_key_exists($short, $data)) {
            $data[$long] = $data[$short];
            unset($data[$short]);
        }
    }
    
    // ========== 2. Processar campo "tr" ==========
    if (isset($data['tr'])) {
        error_log("DEBUG: Campo 'tr' encontrado. Tipo: " . gettype($data['tr']) . 
                 ", Valor: " . json_encode($data['tr']));
        
        if (is_array($data['tr'])) {
            $tr = $data['tr'];
            
            // Formato novo: [dias, horas, minutos, status]
            if (count($tr) == 4 && is_numeric($tr[0]) && is_numeric($tr[1]) && is_numeric($tr[2])) {
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
            // Formato antigo: [valor, unidade, status]
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
            // É targetReached (booleano)
            error_log("DEBUG: 'tr' é booleano (targetReached): " . 
                     ($data['tr'] ? 'true' : 'false'));
            
            $data['targetReached'] = $data['tr'];
            unset($data['tr']);
        }
    }
    // Se não tem 'tr' mas tem 'targetReached' (caso direto do ESP)
    elseif (isset($data['targetReached'])) {
        error_log("DEBUG: Campo 'targetReached' encontrado: " . 
                 ($data['targetReached'] ? 'true' : 'false'));
        // Mantém como está, já é booleano
    }
    
    // ========== 3. Expandir mensagens ==========
    if (isset($data['message']) && is_string($data['message'])) {
        $msg = $data['message'];
        if (isset($messageMap[$msg])) {
            $data['message'] = $messageMap[$msg];
        }
    }
    
    // ========== 4. Expandir status ==========
    if (isset($data['status']) && is_string($data['status'])) {
        $status = $data['status'];
        if (isset($messageMap[$status])) {
            $data['status'] = $messageMap[$status];
        } elseif (isset($statusMap[$status])) {
            $data['status'] = $statusMap[$status];
        }
    }
    
    // ========== 5. Expandir stageType ==========
    if (isset($data['stageType']) && is_string($data['stageType']) && isset($stageTypeMap[$data['stageType']])) {
        $data['stageType'] = $stageTypeMap[$data['stageType']];
    }
    
    // ========== 6. Expandir control_status ==========
    if (isset($data['control_status']) && is_array($data['control_status'])) {
        $cs = &$data['control_status'];
        
        // Mapear campos abreviados
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
        
        // Expandir estado do controle
        if (isset($cs['state']) && is_string($cs['state']) && isset($messageMap[$cs['state']])) {
            $cs['state'] = $messageMap[$cs['state']];
        }
    }
    
    error_log("DEBUG decompressStateData OUTPUT: " . json_encode($data));
}

// ==================== ADICIONE ESTA FUNÇÃO LOG ====================
function logData($message, $data = null) {
    $logFile = __DIR__ . '/fermentation_api.log';
    
    $logMessage = date('Y-m-d H:i:s') . " - " . $message . PHP_EOL;
    
    if ($data !== null) {
        if (is_array($data) || is_object($data)) {
            $logMessage .= json_encode($data, JSON_PRETTY_PRINT) . PHP_EOL;
        } else {
            $logMessage .= $data . PHP_EOL;
        }
    }
    
    $logMessage .= "----------------------------------------" . PHP_EOL;
    
    // Escreve no arquivo
    file_put_contents($logFile, $logMessage, FILE_APPEND);
}

/**
 * Limpa registros antigos - versão simplificada e otimizada
 */
function cleanupOldRecords($pdo, $tableName, $configId, $keepCount = 100, $timestampColumn = 'created_at') {
    try {
        // 1. Primeiro verifica se precisa limpar (otimização)
        $sql = "SELECT COUNT(*) as total FROM {$tableName} WHERE config_id = ?";
        $stmt = $pdo->prepare($sql);
        $stmt->execute([$configId]);
        $count = $stmt->fetch()['total'];
        
        // Só limpa se tiver mais que 20% acima do limite
        if ($count <= ($keepCount * 1.2)) {
            return;
        }
        
        // 2. Encontra o ID mais antigo que deve ser mantido
        $sql = "
            SELECT id FROM {$tableName} 
            WHERE config_id = ? 
            ORDER BY {$timestampColumn} DESC 
            LIMIT 1 OFFSET ?
        ";
        $stmt = $pdo->prepare($sql);
        $stmt->execute([$configId, $keepCount - 1]);
        $result = $stmt->fetch();
        
        if ($result && isset($result['id'])) {
            $oldestIdToKeep = $result['id'];
            
            // 3. Deleta tudo mais antigo que esse ID
            $sql = "
                DELETE FROM {$tableName} 
                WHERE config_id = ? 
                AND {$timestampColumn} < (
                    SELECT {$timestampColumn} 
                    FROM {$tableName} 
                    WHERE id = ?
                )
                LIMIT 500
            ";
            
            $stmt = $pdo->prepare($sql);
            $stmt->execute([$configId, $oldestIdToKeep]);
            
            $deleted = $stmt->rowCount();
            if ($deleted > 0) {
                error_log("[CLEANUP] {$tableName}: mantidos {$keepCount}, removidos {$deleted}");
            }
        }
        
        // 4. Limpa órfãos ocasionalmente (apenas 10% das vezes)
        if (rand(1, 10) === 1) {
            $pdo->prepare("DELETE FROM {$tableName} WHERE config_id IS NULL LIMIT 50")->execute();
        }
        
    } catch (Exception $e) {
        error_log("[CLEANUP ERROR] {$tableName}: " . $e->getMessage());
    }
}

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

function sendResponse($data, $code = 200, $options = 0) {
    http_response_code($code);
    echo json_encode($data, $options);
    exit;
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
            'input' => $originalData,
            'output' => $testData
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
            // RESET COMPLETO AO REINICIAR FERMENTAÇÃO
            
            $updateData['started_at'] = date('Y-m-d H:i:s');
            $updateData['paused_at'] = null;
            $updateData['completed_at'] = null;
            $updateData['current_stage_index'] = 0;
            
            // Reseta todas as etapas
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'pending',
                    start_time = NULL,
                    end_time = NULL
                WHERE config_id = ?
            ");
            $stmt->execute([$configId]);
            
            // Ativa primeira etapa
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'running', start_time = NOW() 
                WHERE config_id = ? AND stage_index = 0
            ");
            $stmt->execute([$configId]);
            
            // Incrementa contador de uso
            $stmt = $pdo->prepare("UPDATE configurations SET times_used = times_used + 1 WHERE id = ?");
            $stmt->execute([$configId]);
            
            // LIMPEZA COMPLETA - Remove TUDO da fermentação anterior
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
        SELECT * FROM readings 
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
    
    // Busca histórico de estados do controlador (últimas 24h)
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
    
    sendResponse([
        'config' => array_merge($config, ['stages' => $stages]),
        'state' => $stateData,
        'readings' => $readings,
        'ispindel' => $ispindel ?: null,
        'controller' => $controller ?: null,
        'controller_history' => $controllerHistory ?: [],
        'heartbeat' => $heartbeat ?: null,
        'is_online' => $isOnline,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

// ==================== LEITURAS (COM LIMPEZA AUTOMÁTICA) ====================

if ($path === 'readings' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    $tempFridge = $input['temp_fridge'] ?? null;
    $tempFermenter = $input['temp_fermenter'] ?? null;
    $tempTarget = $input['temp_target'] ?? null;
    $gravity = $input['gravity'] ?? null;
    
    if (!$configId || $tempFridge === null || $tempFermenter === null || $tempTarget === null) {
        sendResponse(['error' => 'Dados incompletos'], 400);
    }
    
    $stmt = $pdo->prepare("
        INSERT INTO readings (config_id, temp_fridge, temp_fermenter, temp_target, gravity)
        VALUES (?, ?, ?, ?, ?)
    ");
    $stmt->execute([$configId, $tempFridge, $tempFermenter, $tempTarget, $gravity]);
    
    // ✅ Limpa registros antigos E órfãos
    cleanupOldRecords($pdo, 'readings', $configId, 200, 'reading_timestamp'); //Mantém últimos 200
    
    sendResponse(['success' => true, 'reading_id' => $pdo->lastInsertId()], 201);
}

// ==================== ISPINDEL (COM LIMPEZA AUTOMÁTICA) ====================

if ($path === 'ispindel/data' && $method === 'POST') {
    $name = $input['name'] ?? 'iSpindel';
    $temperature = $input['temperature'] ?? null;
    $gravity = $input['gravity'] ?? null;
    $battery = $input['battery'] ?? null;
    $angle = $input['angle'] ?? null;
    
    if ($temperature === null || $gravity === null) {
        sendResponse(['error' => 'temperature and gravity are required'], 400);
    }
    
    try {
        // Busca fermentação ativa
        $stmt = $pdo->prepare("
            SELECT c.id FROM configurations c
            WHERE c.status = 'active'
            LIMIT 1
        ");
        $stmt->execute();
        $activeConfig = $stmt->fetch();
        
        $configId = $activeConfig ? $activeConfig['id'] : null;
        
        // Insere leitura
        $stmt = $pdo->prepare("
            INSERT INTO ispindel_readings (config_id, name, temperature, gravity, battery, angle)
            VALUES (?, ?, ?, ?, ?, ?)
        ");
        $stmt->execute([$configId, $name, $temperature, $gravity, $battery, $angle]);
        
        // ✅ Limpa registros antigos E órfãos
        if ($configId) {
            cleanupOldRecords($pdo, 'ispindel_readings', $configId, 500, 'reading_timestamp');
        }
        
        sendResponse(['success' => true, 'message' => 'iSpindel data saved'], 201);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Error saving iSpindel data: ' . $e->getMessage()], 500);
    }
}

// ==================== CONTROLE (COM LIMPEZA AUTOMÁTICA) ====================

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
    
    // ✅ Limpa registros antigos E órfãos
    cleanupOldRecords($pdo, 'controller_states', $configId, 200, 'state_timestamp');
    
    sendResponse(['success' => true], 201);
}

// ==================== ESTADO FERMENTAÇÃO (COM LIMPEZA AUTOMÁTICA) ====================
if ($path === 'fermentation-state' && $method === 'POST') {
    $configId = $input['config_id'] ?? $input['cid'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    // ✅ LOG 1: Dados recebidos do ESP32
    error_log("=== FERMENTATION-STATE RECEBIDO ===");
    error_log("Config ID: " . $configId);
    error_log("Dados recebidos (CRUS): " . json_encode($input));
    
    // Descomprimir dados recebidos
    decompressStateData($input);
    
    // ✅ LOG 2: Dados após descompressão
    error_log("Dados descomprimidos: " . json_encode($input));
    
    // ✅ LOG 3: Campos específicos para debug
    $debugInfo = [];
    
    if (isset($input['tr'])) {
        $debugInfo['tr_type'] = gettype($input['tr']);
        $debugInfo['tr_value'] = $input['tr'];
    }
    
    if (isset($input['timeRemaining'])) {
        $debugInfo['timeRemaining'] = $input['timeRemaining'];
    }
    
    if (isset($input['targetReached'])) {
        $debugInfo['targetReached'] = $input['targetReached'] ? 'true' : 'false';
    }
    
    if (isset($input['status'])) {
        $debugInfo['status'] = $input['status'];
    }
    
    error_log("DEBUG ESPECÍFICO: " . json_encode($debugInfo));
    error_log("=== FIM DOS DADOS ===");
    
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states (config_id, state_data)
        VALUES (?, ?)
    ");
    $stmt->execute([$configId, json_encode($input)]);
    
    cleanupOldRecords($pdo, 'fermentation_states', $configId, 100, 'state_timestamp');
    
    sendResponse(['success' => true], 201);
}

// ==================== HEARTBEAT (COM LIMPEZA AGRESSIVA) ====================

if ($path === 'heartbeat' && $method === 'POST') {
    $configId = $input['config_id'] ?? $input['cid'] ?? null;
    $uptime = $input['uptime'] ?? null;
    $freeHeap = $input['free_heap'] ?? null;
    $controlStatus = $input['control_status'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    try {
        // Descomprimir control_status se necessário
        if ($controlStatus && is_array($controlStatus)) {
            decompressStateData($controlStatus);
        }
        
        $stmt = $pdo->prepare("
            INSERT INTO esp_heartbeat (config_id, uptime, free_heap, control_status)
            VALUES (?, ?, ?, ?)
        ");
        $stmt->execute([
            $configId, 
            $uptime, 
            $freeHeap,
            $controlStatus ? json_encode($controlStatus) : null
        ]);
        
        cleanupOldRecords($pdo, 'esp_heartbeat', $configId, 50, 'heartbeat_timestamp');
        
        sendResponse(['success' => true], 201);
        
    } catch (Exception $e) {
        sendResponse(['error' => 'Error saving heartbeat: ' . $e->getMessage()], 500);
    }
}

// ==================== LIMPEZA MANUAL (ENDPOINT ADICIONAL) ====================

if ($path === 'cleanup' && $method === 'POST') {
    $userId = requireAuth();
    
    try {
        // Busca todas as configurações do usuário
        $stmt = $pdo->prepare("SELECT id FROM configurations WHERE user_id = ?");
        $stmt->execute([$userId]);
        $configs = $stmt->fetchAll();
        
        $cleaned = 0;
        foreach ($configs as $config) {
            $configId = $config['id'];
            
            // Força limpeza de todas as tabelas (incluindo órfãos)
            cleanupOldRecords($pdo, 'readings', $configId, 500, 'reading_timestamp');
            cleanupOldRecords($pdo, 'controller_states', $configId, 200, 'state_timestamp');
            cleanupOldRecords($pdo, 'fermentation_states', $configId, 100, 'state_timestamp');
            cleanupOldRecords($pdo, 'esp_heartbeat', $configId, 50, 'heartbeat_timestamp');
            cleanupOldRecords($pdo, 'ispindel_readings', $configId, 500, 'reading_timestamp');
            
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

// ==================== ROTA NÃO ENCONTRADA ====================

http_response_code(404);
echo json_encode(['error' => 'Rota não encontrada']);