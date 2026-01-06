<?php
// api/esp/sensors.php - Recebe lista de sensores detectados
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA';

$input = json_decode(file_get_contents('php://input'), true);

if (!isset($input['sensors']) || !is_array($input['sensors'])) {
    http_response_code(400);
    echo json_encode(['error' => 'Invalid sensors data']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Salva ou atualiza dispositivo
    $stmt = $pdo->prepare("
        INSERT INTO devices (device_id, device_type, last_seen, is_online)
        VALUES ('esp8266_main', 'controller', NOW(), TRUE)
        ON DUPLICATE KEY UPDATE 
            last_seen = NOW(),
            is_online = TRUE
    ");
    $stmt->execute();
    
    // Opcional: salvar sensores em tabela separada ou em JSON
    // Por ora, apenas confirma recebimento
    
    echo json_encode([
        'success' => true,
        'sensors_received' => count($input['sensors'])
    ]);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}