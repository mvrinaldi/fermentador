<?php
// api/esp/state.php - Atualiza estado da fermentação
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

$config_id = $_GET['config_id'] ?? null;
$input = json_decode(file_get_contents('php://input'), true);

if (!$config_id) {
    http_response_code(400);
    echo json_encode(['error' => 'config_id is required']);
    exit;
}

// Inicializar resposta
$response = ['success' => false, 'message' => ''];

try {
    // Conectar ao banco de dados
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Salva estado
    $stmt = $pdo->prepare("
        INSERT INTO fermentation_states (config_id, state_data)
        VALUES (?, ?)
    ");
    
    $stmt->execute([
        $config_id,
        json_encode($input)
    ]);
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    error_log("State error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}