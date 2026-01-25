<?php
// api/esp/ispindel.php - Recebe dados do iSpindel
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

error_log("ispindel.php - Recebido: " . print_r($input, true));

$name = $input['name'] ?? 'iSpindel';
$temperature = $input['temperature'] ?? null;
$gravity = $input['gravity'] ?? null;
$battery = $input['battery'] ?? null;
$angle = $input['angle'] ?? null;

if ($temperature === null || $gravity === null) {
    http_response_code(400);
    echo json_encode(['error' => 'temperature and gravity are required']);
    exit;
}

// Inicializar resposta
$response = ['success' => false, 'message' => ''];

try {
    // Conectar ao banco de dados
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
               DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Insere leitura
    $stmt = $pdo->prepare("
        INSERT INTO ispindel_readings (name, temperature, gravity, battery, angle)
        VALUES (?, ?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $name,
        $temperature,
        $gravity,
        $battery,
        $angle
    ]);
    
    // Atualiza última leitura global (para acesso rápido)
    $stmt = $pdo->prepare("
        INSERT INTO devices (device_id, device_type, sensor_data, last_seen, is_online)
        VALUES ('ispindel', 'sensor', ?, NOW(), TRUE)
        ON DUPLICATE KEY UPDATE 
            sensor_data = VALUES(sensor_data),
            last_seen = NOW(),
            is_online = TRUE
    ");
    
    $ispindelData = json_encode([
        'name' => $name,
        'temperature' => $temperature,
        'gravity' => $gravity,
        'battery' => $battery,
        'angle' => $angle
    ]);
    
    $stmt->execute([$ispindelData]);
    
    echo json_encode([
        'success' => true,
        'message' => 'iSpindel data saved'
    ]);
    
} catch (PDOException $e) {
    error_log("iSpindel error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}