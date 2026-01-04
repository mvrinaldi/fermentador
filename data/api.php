<?php
// api.php - API Backend para Sistema de Fermentação
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE');
header('Access-Control-Allow-Headers: Content-Type, Authorization');

// Configuração do banco de dados
$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = '6c3@5mZ8';

// Conexão com o banco
try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $pdo->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Erro de conexão com banco de dados']);
    exit;
}

// Pegar método e endpoint
$method = $_SERVER['REQUEST_METHOD'];
$path = isset($_GET['path']) ? $_GET['path'] : '';
$input = json_decode(file_get_contents('php://input'), true);

// Autenticação simples via sessão
session_start();

// ==================== FUNÇÕES AUXILIARES ====================

function requireAuth() {
    if (!isset($_SESSION['user_id'])) {
        http_response_code(401);
        echo json_encode(['error' => 'Não autenticado']);
        exit;
    }
    return $_SESSION['user_id'];
}

function sendResponse($data, $code = 200) {
    http_response_code($code);
    echo json_encode($data);
    exit;
}

// ==================== ROTAS ====================

// LOGIN
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
        $_SESSION['user_id'] = $user['id'];
        
        // Atualizar último login
        $stmt = $pdo->prepare("UPDATE users SET last_login = NOW() WHERE id = ?");
        $stmt->execute([$user['id']]);
        
        sendResponse(['success' => true, 'user_id' => $user['id']]);
    } else {
        sendResponse(['error' => 'Credenciais inválidas'], 401);
    }
}

// LOGOUT
if ($path === 'auth/logout' && $method === 'POST') {
    session_destroy();
    sendResponse(['success' => true]);
}

// CHECK AUTH STATUS
if ($path === 'auth/check' && $method === 'GET') {
    if (isset($_SESSION['user_id'])) {
        sendResponse(['authenticated' => true, 'user_id' => $_SESSION['user_id']]);
    } else {
        sendResponse(['authenticated' => false]);
    }
}

// ==================== CONFIGURAÇÕES ====================

// LISTAR CONFIGURAÇÕES
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
    
    // Carregar stages para cada configuração
    foreach ($configs as &$config) {
        $stmt = $pdo->prepare("SELECT * FROM stages WHERE config_id = ? ORDER BY stage_index");
        $stmt->execute([$config['id']]);
        $config['stages'] = $stmt->fetchAll();
    }
    
    sendResponse($configs);
}

// CRIAR CONFIGURAÇÃO
if ($path === 'configurations' && $method === 'POST') {
    $userId = requireAuth();
    
    $name = $input['name'] ?? '';
    $stages = $input['stages'] ?? [];
    
    if (empty($name) || empty($stages)) {
        sendResponse(['error' => 'Nome e etapas são obrigatórios'], 400);
    }
    
    $pdo->beginTransaction();
    
    try {
        // Criar configuração
        $stmt = $pdo->prepare("
            INSERT INTO configurations (user_id, name, status, current_stage_index, times_used)
            VALUES (?, ?, 'pending', 0, 0)
        ");
        $stmt->execute([$userId, $name]);
        $configId = $pdo->lastInsertId();
        
        // Criar stages
        foreach ($stages as $index => $stage) {
            $stmt = $pdo->prepare("
                INSERT INTO stages (
                    config_id, stage_index, type, target_temp, duration, 
                    target_gravity, max_duration, start_temp, ramp_time, 
                    max_ramp_rate, actual_rate, direction, status
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending')
            ");
            
            $stmt->execute([
                $configId,
                $index,
                $stage['type'],
                $stage['targetTemp'] ?? null,
                $stage['duration'] ?? null,
                $stage['targetGravity'] ?? null,
                $stage['maxDuration'] ?? null,
                $stage['startTemp'] ?? null,
                $stage['rampTime'] ?? null,
                $stage['maxRampRate'] ?? null,
                $stage['actualRate'] ?? null,
                $stage['direction'] ?? null
            ]);
        }
        
        $pdo->commit();
        
        // Log da ação
        $stmt = $pdo->prepare("
            INSERT INTO action_history (user_id, config_id, action_type, action_details)
            VALUES (?, ?, 'create_configuration', ?)
        ");
        $stmt->execute([$userId, $configId, json_encode(['name' => $name])]);
        
        sendResponse(['success' => true, 'config_id' => $configId], 201);
        
    } catch (Exception $e) {
        $pdo->rollBack();
        sendResponse(['error' => 'Erro ao criar configuração: ' . $e->getMessage()], 500);
    }
}

// ATUALIZAR STATUS DA CONFIGURAÇÃO
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
            
            // Atualizar primeira stage para running
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'running', start_time = NOW() 
                WHERE config_id = ? AND stage_index = 0
            ");
            $stmt->execute([$configId]);
            
            // Incrementar times_used
            $stmt = $pdo->prepare("UPDATE configurations SET times_used = times_used + 1 WHERE id = ?");
            $stmt->execute([$configId]);
            
        } elseif ($status === 'paused') {
            $updateData['paused_at'] = date('Y-m-d H:i:s');
        } elseif ($status === 'completed') {
            $updateData['completed_at'] = date('Y-m-d H:i:s');
        }
        
        // Construir query dinâmica
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
        
        // Log
        $stmt = $pdo->prepare("
            INSERT INTO action_history (user_id, config_id, action_type, action_details)
            VALUES (?, ?, 'update_status', ?)
        ");
        $stmt->execute([$userId, $configId, json_encode(['new_status' => $status])]);
        
        sendResponse(['success' => true]);
        
    } catch (Exception $e) {
        $pdo->rollBack();
        sendResponse(['error' => 'Erro ao atualizar status: ' . $e->getMessage()], 500);
    }
}

// DELETAR CONFIGURAÇÃO
if ($path === 'configurations/delete' && $method === 'DELETE') {
    $userId = requireAuth();
    $configId = $input['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'ID é obrigatório'], 400);
    }
    
    // Verificar se não está ativa
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

// OBTER FERMENTAÇÃO ATIVA
if ($path === 'active' && $method === 'GET') {
    $userId = requireAuth();
    
    $stmt = $pdo->prepare("
        SELECT af.*, c.name, c.current_stage_index, c.status
        FROM active_fermentations af
        JOIN configurations c ON af.config_id = c.id
        WHERE c.user_id = ? AND af.deactivated_at IS NULL
        ORDER BY af.activated_at DESC
        LIMIT 1
    ");
    $stmt->execute([$userId]);
    $active = $stmt->fetch();
    
    if ($active) {
        sendResponse([
            'active' => true,
            'id' => $active['config_id'],
            'name' => $active['name'],
            'currentStageIndex' => $active['current_stage_index']
        ]);
    } else {
        sendResponse(['active' => false, 'id' => null]);
    }
}

// ATIVAR FERMENTAÇÃO
if ($path === 'active/activate' && $method === 'POST') {
    $userId = requireAuth();
    $configId = $input['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'ID é obrigatório'], 400);
    }
    
    $pdo->beginTransaction();
    
    try {
        // Desativar qualquer fermentação ativa
        $stmt = $pdo->prepare("UPDATE active_fermentations SET deactivated_at = NOW() WHERE deactivated_at IS NULL");
        $stmt->execute();
        
        // Ativar nova
        $stmt = $pdo->prepare("INSERT INTO active_fermentations (config_id) VALUES (?)");
        $stmt->execute([$configId]);
        
        // Atualizar status da configuração para active
        $stmt = $pdo->prepare("UPDATE configurations SET status = 'active', started_at = NOW() WHERE id = ?");
        $stmt->execute([$configId]);
        
        $pdo->commit();
        sendResponse(['success' => true]);
        
    } catch (Exception $e) {
        $pdo->rollBack();
        sendResponse(['error' => 'Erro ao ativar fermentação: ' . $e->getMessage()], 500);
    }
}

// DESATIVAR FERMENTAÇÃO
if ($path === 'active/deactivate' && $method === 'POST') {
    requireAuth();
    
    $stmt = $pdo->prepare("UPDATE active_fermentations SET deactivated_at = NOW() WHERE deactivated_at IS NULL");
    $stmt->execute();
    
    sendResponse(['success' => true]);
}

// ==================== LEITURAS ====================

// OBTER LEITURAS
if ($path === 'readings' && $method === 'GET') {
    requireAuth();
    $configId = $_GET['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    // Limitar últimos 30 dias
    $stmt = $pdo->prepare("
        SELECT * FROM readings 
        WHERE config_id = ? 
        AND reading_timestamp >= DATE_SUB(NOW(), INTERVAL 30 DAY)
        ORDER BY reading_timestamp ASC
    ");
    $stmt->execute([$configId]);
    $readings = $stmt->fetchAll();
    
    sendResponse($readings);
}

// ADICIONAR LEITURA (para ESP32)
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
    
    sendResponse(['success' => true, 'reading_id' => $pdo->lastInsertId()], 201);
}

// ==================== ESTADO DO CONTROLADOR ====================

// OBTER ESTADO DO CONTROLADOR
if ($path === 'control' && $method === 'GET') {
    requireAuth();
    
    $stmt = $pdo->prepare("
        SELECT * FROM controller_states 
        ORDER BY state_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute();
    $state = $stmt->fetch();
    
    sendResponse($state ?: ['setpoint' => null, 'cooling' => false, 'heating' => false]);
}

// ATUALIZAR ESTADO DO CONTROLADOR (para ESP32)
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
    
    sendResponse(['success' => true], 201);
}

// ==================== ESTADO DA FERMENTAÇÃO ====================

// OBTER ESTADO DA FERMENTAÇÃO
if ($path === 'fermentation-state' && $method === 'GET') {
    requireAuth();
    $configId = $_GET['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    $stmt = $pdo->prepare("
        SELECT * FROM fermentation_states 
        WHERE config_id = ?
        ORDER BY state_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $state = $stmt->fetch();
    
    if ($state && $state['stage_timers']) {
        $state['stage_timers'] = json_decode($state['stage_timers'], true);
    }
    
    sendResponse($state ?: []);
}

// ATUALIZAR ESTADO DA FERMENTAÇÃO (para ESP32)
if ($path === 'fermentation-state' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    $timeRemainingValue = $input['time_remaining_value'] ?? null;
    $timeRemainingUnit = $input['time_remaining_unit'] ?? null;
    $currentTargetTemp = $input['current_target_temp'] ?? null;
    $stageTimers = $input['stage_timers'] ?? [];
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states 
        (config_id, time_remaining_value, time_remaining_unit, current_target_temp, stage_timers)
        VALUES (?, ?, ?, ?, ?)
    ");
    $stmt->execute([
        $configId, 
        $timeRemainingValue, 
        $timeRemainingUnit, 
        $currentTargetTemp,
        json_encode($stageTimers)
    ]);
    
    sendResponse(['success' => true], 201);
}

// ==================== LIMPEZA DE DADOS ANTIGOS ====================

if ($path === 'cleanup' && $method === 'POST') {
    requireAuth();
    
    // Deletar leituras com mais de 30 dias
    $stmt = $pdo->prepare("
        DELETE FROM readings 
        WHERE reading_timestamp < DATE_SUB(NOW(), INTERVAL 30 DAY)
    ");
    $stmt->execute();
    $deletedReadings = $stmt->rowCount();
    
    // Deletar estados antigos do controlador (manter apenas últimos 7 dias)
    $stmt = $pdo->prepare("
        DELETE FROM controller_states 
        WHERE state_timestamp < DATE_SUB(NOW(), INTERVAL 7 DAY)
    ");
    $stmt->execute();
    $deletedStates = $stmt->rowCount();
    
    sendResponse([
        'success' => true,
        'deleted_readings' => $deletedReadings,
        'deleted_controller_states' => $deletedStates
    ]);
}

// ==================== ROTA NÃO ENCONTRADA ====================

http_response_code(404);
echo json_encode(['error' => 'Rota não encontrada']);