<?php
// api/esp/config.php - Retorna configuração otimizada para ESP8266
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA';

$configId = $_GET['id'] ?? null;

if (!$configId) {
    http_response_code(400);
    echo json_encode(['error' => 'config_id required']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Busca configuração (inclui status para verificar pause/complete)
    $stmt = $pdo->prepare("
        SELECT name, current_stage_index, current_target_temp, status
        FROM configurations
        WHERE id = ?
        LIMIT 1
    ");
    $stmt->execute([$configId]);
    $config = $stmt->fetch(PDO::FETCH_ASSOC);
    
    if (!$config) {
        http_response_code(404);
        echo json_encode(['error' => 'Configuration not found']);
        exit;
    }
    
    // Busca stages (apenas campos essenciais)
    $stmt = $pdo->prepare("
        SELECT 
            stage_index,
            type,
            target_temp,
            start_temp,
            duration,
            ramp_time,
            target_gravity,
            max_duration as timeout_days,
            status
        FROM stages
        WHERE config_id = ?
        ORDER BY stage_index ASC
    ");
    $stmt->execute([$configId]);
    $stages = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // Resposta compacta
    $response = [
        'name' => $config['name'],
        'status' => $config['status'], // Para detectar pause/complete
        'currentStageIndex' => (int)$config['current_stage_index'],
        'currentTargetTemp' => (float)$config['current_target_temp'],
        'stages' => []
    ];
    
    foreach ($stages as $stage) {
        $response['stages'][] = [
            'type' => $stage['type'],
            'targetTemp' => (float)$stage['target_temp'],
            'startTemp' => (float)$stage['start_temp'],
            'duration' => (int)$stage['duration'],
            'rampTime' => (int)$stage['ramp_time'],
            'targetGravity' => (float)$stage['target_gravity'],
            'timeoutDays' => (int)$stage['timeout_days'],
            'status' => $stage['status']
        ];
    }
    
    echo json_encode($response);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}