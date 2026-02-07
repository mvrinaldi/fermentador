<?php
/**
 * API de Alertas - Versão Simplificada
 * 
 * @author Marcos Rinaldi
 * @version 1.2 - Removido min_level (todos alertas são enviados)
 */

// Headers
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Caminho flexível para AlertSystem.php
$alertSystemPaths = [
    __DIR__ . '/../classes/AlertSystem.php',
    __DIR__ . '/../AlertSystem.php',
    dirname(dirname(__DIR__)) . '/classes/AlertSystem.php'
];

$alertSystemFile = null;
foreach ($alertSystemPaths as $path) {
    if (file_exists($path)) {
        $alertSystemFile = $path;
        break;
    }
}

if (!$alertSystemFile) {
    http_response_code(500);
    echo json_encode([
        'error' => 'AlertSystem.php not found',
        'searched_paths' => $alertSystemPaths
    ]);
    exit;
}

require_once $alertSystemFile;

if (!class_exists('AlertSystem')) {
    http_response_code(500);
    echo json_encode(['error' => 'Class AlertSystem not found after inclusion']);
    exit;
}

// Conexão com banco
$dbConfig = __DIR__ . '/../config/database.php';
if (!file_exists($dbConfig)) {
    http_response_code(500);
    echo json_encode(['error' => 'Database config not found at: ' . $dbConfig]);
    exit;
}

require_once $dbConfig;

try {
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
        DB_USER,
        DB_PASS,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database connection failed: ' . $e->getMessage()]);
    exit;
}

// Inicializa sistema de alertas
$alertSystem = new AlertSystem($pdo);

// Obtém o path da requisição
$path = $_GET['action'] ?? '';
$method = $_SERVER['REQUEST_METHOD'];

// Router simples
try {
    switch ($path) {
        
        // GET /alerts - Lista alertas não lidos
        case '':
        case 'unread':
            if ($method !== 'GET') {
                throw new Exception('Method not allowed', 405);
            }
            
            $configId = $_GET['config_id'] ?? null;
            $alerts = $alertSystem->getUnreadAlerts($configId);
            
            echo json_encode([
                'success' => true,
                'count' => count($alerts),
                'alerts' => $alerts
            ]);
            break;
        
        // GET /alerts/all - Lista todos os alertas
        case 'all':
            if ($method !== 'GET') {
                throw new Exception('Method not allowed', 405);
            }
            
            $configId = $_GET['config_id'] ?? null;
            $limit = min(intval($_GET['limit'] ?? 100), 500);
            
            $sql = "SELECT * FROM alerts";
            $params = [];
            
            if ($configId) {
                $sql .= " WHERE config_id = ?";
                $params[] = $configId;
            }
            
            $sql .= " ORDER BY created_at DESC LIMIT " . $limit;
            
            $stmt = $pdo->prepare($sql);
            $stmt->execute($params);
            $alerts = $stmt->fetchAll(PDO::FETCH_ASSOC);
            
            echo json_encode([
                'success' => true,
                'count' => count($alerts),
                'alerts' => $alerts
            ]);
            break;
        
        // GET /alerts/check - Executa verificação de alertas
        case 'check':
            if ($method !== 'GET') {
                throw new Exception('Method not allowed', 405);
            }
            
            // Buscar fermentação ativa
            $stmt = $pdo->query("
                SELECT id FROM configurations 
                WHERE status = 'active' 
                LIMIT 1
            ");
            $config = $stmt->fetch(PDO::FETCH_ASSOC);
            
            if (!$config) {
                echo json_encode([
                    'success' => true,
                    'message' => 'No active fermentation',
                    'alerts' => []
                ]);
                break;
            }
            
            // Executar verificação
            $alerts = $alertSystem->checkAll($config['id']);
            
            echo json_encode([
                'success' => true,
                'config_id' => $config['id'],
                'alerts_generated' => count($alerts),
                'alerts' => $alerts
            ]);
            break;
        
        // POST /alerts/read - Marca alerta como lido
        case 'read':
            if ($method !== 'POST') {
                throw new Exception('Method not allowed', 405);
            }
            
            $input = json_decode(file_get_contents('php://input'), true);
            $alertId = $input['alert_id'] ?? $_GET['id'] ?? null;
            
            if (!$alertId) {
                throw new Exception('alert_id is required', 400);
            }
            
            $alertSystem->markAsRead($alertId);
            
            echo json_encode([
                'success' => true,
                'message' => 'Alert marked as read'
            ]);
            break;
        
        // POST /alerts/read-all - Marca todos como lidos
        case 'read-all':
            if ($method !== 'POST') {
                throw new Exception('Method not allowed', 405);
            }
            
            $input = json_decode(file_get_contents('php://input'), true);
            $configId = $input['config_id'] ?? null;
            
            $alertSystem->markAllAsRead($configId);
            
            echo json_encode([
                'success' => true,
                'message' => 'All alerts marked as read'
            ]);
            break;
        
        // GET/POST /alerts/config - Configuração de notificações
        case 'config':
            if ($method === 'GET') {
                $stmt = $pdo->query("
                    SELECT config_key, config_value 
                    FROM system_config 
                    WHERE config_key LIKE 'alert_%'
                ");
                
                $config = [];
                while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
                    $key = str_replace('alert_', '', $row['config_key']);
                    // Ocultar API keys parcialmente
                    if (strpos($key, 'apikey') !== false || strpos($key, 'token') !== false) {
                        $value = $row['config_value'];
                        if (strlen($value) > 8) {
                            $value = substr($value, 0, 4) . '****' . substr($value, -4);
                        }
                        $config[$key] = $value;
                        $config[$key . '_set'] = !empty($row['config_value']);
                    } else {
                        $config[$key] = $row['config_value'];
                    }
                }
                
                echo json_encode([
                    'success' => true,
                    'config' => $config
                ]);
            }
            elseif ($method === 'POST') {
                $input = json_decode(file_get_contents('php://input'), true);
                
                if (!$input) {
                    throw new Exception('Invalid JSON', 400);
                }
                
                // ✅ Campos permitidos (removido min_level)
                $allowedFields = [
                    'enabled',
                    'whatsapp_phone', 'whatsapp_apikey',
                    'telegram_bot_token', 'telegram_chat_id',
                    'temp_tolerance', 'temp_critical_tolerance',
                    'esp_offline_seconds', 'cooldown_minutes'
                ];
                
                $stmt = $pdo->prepare("
                    INSERT INTO system_config (config_key, config_value, updated_at)
                    VALUES (?, ?, NOW())
                    ON DUPLICATE KEY UPDATE config_value = VALUES(config_value), updated_at = NOW()
                ");
                
                $updated = 0;
                foreach ($input as $key => $value) {
                    if (in_array($key, $allowedFields)) {
                        $stmt->execute(['alert_' . $key, $value]);
                        $updated++;
                    }
                }
                
                echo json_encode([
                    'success' => true,
                    'message' => "{$updated} settings updated"
                ]);
            }
            else {
                throw new Exception('Method not allowed', 405);
            }
            break;
        
        // POST /alerts/test - Envia notificação de teste
        case 'test':
            if ($method !== 'POST') {
                throw new Exception('Method not allowed', 405);
            }
            
            $input = json_decode(file_get_contents('php://input'), true);
            $type = $input['type'] ?? 'both'; // whatsapp, telegram, both
            
            // Buscar configurações
            $stmt = $pdo->query("
                SELECT config_key, config_value 
                FROM system_config 
                WHERE config_key LIKE 'alert_%'
            ");
            
            $config = [];
            while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
                $key = str_replace('alert_', '', $row['config_key']);
                $config[$key] = $row['config_value'];
            }
            
            $results = [];
            $testMessage = "🧪 TESTE: Sistema de alertas funcionando! " . 
                          date('d/m/Y H:i:s');
            
            // Teste WhatsApp
            if (($type === 'whatsapp' || $type === 'both') && 
                !empty($config['whatsapp_phone']) && 
                !empty($config['whatsapp_apikey'])) {
                
                $url = "https://api.callmebot.com/whatsapp.php?" . http_build_query([
                    'phone' => $config['whatsapp_phone'],
                    'text' => $testMessage,
                    'apikey' => $config['whatsapp_apikey']
                ]);
                
                $ch = curl_init();
                curl_setopt_array($ch, [
                    CURLOPT_URL => $url,
                    CURLOPT_RETURNTRANSFER => true,
                    CURLOPT_TIMEOUT => 15
                ]);
                
                $response = curl_exec($ch);
                $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
                $error = curl_error($ch);
                curl_close($ch);
                
                $results['whatsapp'] = [
                    'sent' => $httpCode === 200 && strpos($response, 'Message queued') !== false,
                    'http_code' => $httpCode,
                    'response' => substr($response, 0, 200),
                    'error' => $error ?: null
                ];
            }
            
            // Teste Telegram - ✅ SEM parse_mode HTML
            if (($type === 'telegram' || $type === 'both') && 
                !empty($config['telegram_bot_token']) && 
                !empty($config['telegram_chat_id'])) {
                
                $url = "https://api.telegram.org/bot{$config['telegram_bot_token']}/sendMessage";
                
                $ch = curl_init();
                curl_setopt_array($ch, [
                    CURLOPT_URL => $url,
                    CURLOPT_POST => true,
                    CURLOPT_POSTFIELDS => http_build_query([
                        'chat_id' => $config['telegram_chat_id'],
                        'text' => $testMessage
                    ]),
                    CURLOPT_RETURNTRANSFER => true,
                    CURLOPT_TIMEOUT => 15
                ]);
                
                $response = curl_exec($ch);
                $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
                $error = curl_error($ch);
                curl_close($ch);
                
                $responseData = json_decode($response, true);
                
                $results['telegram'] = [
                    'sent' => $httpCode === 200 && ($responseData['ok'] ?? false),
                    'http_code' => $httpCode,
                    'response' => $responseData,
                    'error' => $error ?: null
                ];
            }
            
            if (empty($results)) {
                throw new Exception('No notification service configured', 400);
            }
            
            echo json_encode([
                'success' => true,
                'message' => $testMessage,
                'results' => $results
            ]);
            break;
        
        // GET /alerts/stats - Estatísticas de alertas
        case 'stats':
            if ($method !== 'GET') {
                throw new Exception('Method not allowed', 405);
            }
            
            $configId = $_GET['config_id'] ?? null;
            
            $whereClause = $configId ? "WHERE config_id = ?" : "";
            $params = $configId ? [$configId] : [];
            
            // Total por tipo
            $stmt = $pdo->prepare("
                SELECT alert_type, alert_level, COUNT(*) as count
                FROM alerts
                {$whereClause}
                GROUP BY alert_type, alert_level
            ");
            $stmt->execute($params);
            $byType = $stmt->fetchAll(PDO::FETCH_ASSOC);
            
            // Não lidos
            $stmt = $pdo->prepare("
                SELECT COUNT(*) as unread
                FROM alerts
                WHERE is_read = 0
                " . ($configId ? "AND config_id = ?" : "")
            );
            $stmt->execute($params);
            $unread = $stmt->fetch(PDO::FETCH_ASSOC)['unread'];
            
            // Últimas 24h
            $stmt = $pdo->prepare("
                SELECT COUNT(*) as last_24h
                FROM alerts
                WHERE created_at > DATE_SUB(NOW(), INTERVAL 24 HOUR)
                " . ($configId ? "AND config_id = ?" : "")
            );
            $stmt->execute($params);
            $last24h = $stmt->fetch(PDO::FETCH_ASSOC)['last_24h'];
            
            echo json_encode([
                'success' => true,
                'stats' => [
                    'unread' => intval($unread),
                    'last_24h' => intval($last24h),
                    'by_type' => $byType
                ]
            ]);
            break;
        
        default:
            throw new Exception('Endpoint not found', 404);
    }
    
} catch (Exception $e) {
    $code = $e->getCode() ?: 500;
    if ($code < 100 || $code > 599) $code = 500;
    
    http_response_code($code);
    echo json_encode([
        'success' => false,
        'error' => $e->getMessage()
    ]);
}