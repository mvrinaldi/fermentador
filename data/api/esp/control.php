<?php
// api/esp/control.php - Atualiza estado do controlador
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA';

$input = json_decode(file_get_contents('php://input'), true);

$configId = $input['config_id'] ?? null;
$setpoint = $input['setpoint'] ?? null;
$cooling = $input['cooling'] ?? false;
$heating = $input['heating'] ?? false;

if (!$configId || $setpoint === null) {
    http_response_code(400);
    echo json_encode(['error' => 'Missing required fields']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    $stmt = $pdo->prepare("
        INSERT INTO controller_states 
        (config_id, setpoint, cooling, heating, is_running)
        VALUES (?, ?, ?, ?, TRUE)
    ");
    
    $stmt->execute([
        $configId,
        $setpoint,
        $cooling ? 1 : 0,
        $heating ? 1 : 0
    ]);
    
    echo json_encode(['success' => true]);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}