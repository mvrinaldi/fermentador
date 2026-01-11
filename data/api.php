<?php
// api.php - API Backend Refatorado (ESP como fonte única de verdade)
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

$method = $_SERVER['REQUEST_METHOD'];
$path = isset($_GET['path']) ? $_GET['path'] : '';
$input = json_decode(file_get_contents('php://input'), true);

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

function sendResponse($data, $code = 200) {
    http_response_code($code);
    echo json_encode($data);
    exit;
}

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
        ]);
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
            // ===== RESET COMPLETO AO REINICIAR FERMENTAÇÃO =====
            
            // 1. Limpa dados de conclusão anterior
            $updateData['started_at'] = date('Y-m-d H:i:s');
            $updateData['paused_at'] = null;
            $updateData['completed_at'] = null;
            $updateData['current_stage_index'] = 0;
            
            // 2. Reseta todas as etapas para status inicial
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'pending',
                    start_time = NULL,
                    end_time = NULL
                WHERE config_id = ?
            ");
            $stmt->execute([$configId]);
            
            // 3. Ativa primeira etapa
            $stmt = $pdo->prepare("
                UPDATE stages 
                SET status = 'running', start_time = NOW() 
                WHERE config_id = ? AND stage_index = 0
            ");
            $stmt->execute([$configId]);
            
            // 4. Incrementa contador de uso
            $stmt = $pdo->prepare("UPDATE configurations SET times_used = times_used + 1 WHERE id = ?");
            $stmt->execute([$configId]);
            
            // 5. OPCIONAL: Limpa leituras antigas (se quiser começar do zero)
            // Descomente se quiser apagar histórico de fermentações anteriores
            /*
            $stmt = $pdo->prepare("DELETE FROM readings WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM ispindel_readings WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM controller_states WHERE config_id = ?");
            $stmt->execute([$configId]);
            
            $stmt = $pdo->prepare("DELETE FROM fermentation_states WHERE config_id = ?");
            $stmt->execute([$configId]);
            */
            
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
    
    // Verifica se a config pertence ao usuário
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

// ==================== NOVO: ESTADO COMPLETO (ESP → FRONTEND) ====================

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
    
    // Busca estado da fermentação (enviado pelo ESP)
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
    
    // Busca estado do controlador
    $stmt = $pdo->prepare("
        SELECT * FROM controller_states 
        WHERE config_id = ?
        ORDER BY state_timestamp DESC 
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $controller = $stmt->fetch();
    
    // Busca último heartbeat do ESP
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
            $isOnline = $diff < 120; // Online se < 2 minutos
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
        'heartbeat' => $heartbeat ?: null,
        'is_online' => $isOnline,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
}

// ==================== LEITURAS (VINCULADAS AO CONFIG_ID) ====================

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

// ==================== ISPINDEL (VINCULADO AO CONFIG_ID) ====================

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
        
        // Insere leitura do iSpindel VINCULADA ao config_id
        $stmt = $pdo->prepare("
            INSERT INTO ispindel_readings (config_id, name, temperature, gravity, battery, angle)
            VALUES (?, ?, ?, ?, ?, ?)
        ");
        $stmt->execute([$configId, $name, $temperature, $gravity, $battery, $angle]);
        
        sendResponse(['success' => true, 'message' => 'iSpindel data saved'], 201);
        
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
    
    sendResponse(['success' => true], 201);
}

// ==================== ESTADO FERMENTAÇÃO (ESP ENVIA) ====================

if ($path === 'fermentation-state' && $method === 'POST') {
    $configId = $input['config_id'] ?? null;
    
    if (!$configId) {
        sendResponse(['error' => 'config_id é obrigatório'], 400);
    }
    
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states (config_id, state_data)
        VALUES (?, ?)
    ");
    $stmt->execute([$configId, json_encode($input)]);
    
    sendResponse(['success' => true], 201);
}

http_response_code(404);
echo json_encode(['error' => 'Rota não encontrada']);