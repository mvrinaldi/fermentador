<?php
// api/esp/active.php - Retorna fermentação ativa (otimizado para ESP8266)
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

// Configuração do banco
$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = 'SENHA'; // Altere para senha real

try {
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Query otimizada: apenas campos necessários + LIMIT 1
    $stmt = $pdo->prepare("
        SELECT 
            af.config_id as id,
            c.current_stage_index,
            c.name
        FROM active_fermentations af
        JOIN configurations c ON af.config_id = c.id
        WHERE af.deactivated_at IS NULL
        AND c.status = 'active'
        ORDER BY af.activated_at DESC
        LIMIT 1
    ");
    
    $stmt->execute();
    $active = $stmt->fetch(PDO::FETCH_ASSOC);
    
    if ($active) {
        // Resposta compacta
        echo json_encode([
            'active' => true,
            'id' => (string)$active['id'],
            'currentStageIndex' => (int)$active['current_stage_index'],
            'name' => $active['name']
        ]);
    } else {
        echo json_encode([
            'active' => false,
            'id' => null
        ]);
    }
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database error']);
}