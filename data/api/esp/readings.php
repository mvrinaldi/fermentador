<?php
// api/esp/reading.php - Recebe leituras do ESP8266
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA';

$input = json_decode(file_get_contents('php://input'), true);

$configId = $input['config_id'] ?? null;
$tempFridge = $input['temp_fridge'] ?? null;
$tempFermenter = $input['temp_fermenter'] ?? null;
$tempTarget = $input['temp_target'] ?? null;
$gravity = $input['gravity'] ?? null;

if (!$configId || $tempFridge === null || $tempFermenter === null || $tempTarget === null) {
    http_response_code(400);
    echo json_encode(['error' => 'Missing required fields']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    $stmt = $pdo->prepare("
        INSERT INTO readings 
        (config_id, temp_fridge, temp_fermenter, temp_target, gravity)
        VALUES (?, ?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $configId,
        $tempFridge,
        $tempFermenter,
        $tempTarget,
        $gravity
    ]);
    
    echo json_encode(['success' => true, 'id' => $pdo->lastInsertId()]);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}