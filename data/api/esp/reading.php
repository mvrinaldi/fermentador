<?php
// api/esp/reading.php - Recebe leituras de temperatura do ESP
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['error' => 'Method not allowed']);
    exit;
}

// Conexão banco de dados
require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

$input = json_decode(file_get_contents('php://input'), true);

// Log para debug
error_log("reading.php - Recebido: " . json_encode($input));

$config_id = $input['config_id'] ?? null;
$temp_fridge = $input['temp_fridge'] ?? null;
$temp_fermenter = $input['temp_fermenter'] ?? null;
$temp_target = $input['temp_target'] ?? null;
$gravity = $input['gravity'] ?? null;

// Validação mínima
if ($temp_fermenter === null || $temp_fridge === null) {
    http_response_code(400);
    echo json_encode(['error' => 'temp_fermenter and temp_fridge are required']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Se não há config_id, busca fermentação ativa
    if (empty($config_id)) {
        $stmt = $pdo->prepare("
            SELECT id FROM configurations 
            WHERE status = 'active' 
            ORDER BY started_at DESC 
            LIMIT 1
        ");
        $stmt->execute();
        $activeConfig = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($activeConfig) {
            $config_id = $activeConfig['id'];
            error_log("reading.php - Usando fermentação ativa: {$config_id}");
        } else {
            // Sem fermentação ativa - apenas loga mas não salva
            error_log("reading.php - Sem fermentação ativa, leitura ignorada");
            echo json_encode([
                'success' => true,
                'message' => 'No active fermentation, reading not saved'
            ]);
            exit;
        }
    }
    
    // Salva leitura no banco
    $stmt = $pdo->prepare("
        INSERT INTO readings (config_id, temp_fridge, temp_fermenter, temp_target, gravity)
        VALUES (?, ?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $config_id,
        $temp_fridge,
        $temp_fermenter,
        $temp_target,
        $gravity
    ]);
    
    $reading_id = $pdo->lastInsertId();
    
    error_log("reading.php - Leitura salva: ID {$reading_id}");
    
    echo json_encode([
        'success' => true,
        'reading_id' => $reading_id,
        'config_id' => $config_id
    ]);
    
} catch (PDOException $e) {
    error_log("reading.php - Database error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode([
        'error' => 'Database error',
        'details' => $e->getMessage()
    ]);
}