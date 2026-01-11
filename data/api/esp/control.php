<?php
// api/esp/control.php - Recebe estado dos relés (CORRIGIDO)
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
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    error_log("Control error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode([
        'error' => 'Database error',
        'details' => $e->getMessage()
    ]);
}