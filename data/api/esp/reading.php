<?php
// api/esp/reading.php
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

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once $_SERVER['DOCUMENT_ROOT'] . '/classes/DatabaseCleanup.php';

$input = json_decode(file_get_contents('php://input'), true);

$config_id = $input['cid'] ?? $input['config_id'] ?? null;
$temp_fridge = $input['tf'] ?? $input['temp_fridge'] ?? null;
$temp_fermenter = $input['tb'] ?? $input['temp_fermenter'] ?? null;
$temp_target = $input['tt'] ?? $input['temp_target'] ?? null;
$gravity = $input['g'] ?? $input['gravity'] ?? null;
$spindel_temp = $input['st'] ?? $input['spindel_temp'] ?? null;
$spindel_battery = $input['sb'] ?? $input['spindel_battery'] ?? null;

if ($temp_fermenter === null || $temp_fridge === null) {
    http_response_code(400);
    echo json_encode(['error' => 'temp_fermenter and temp_fridge are required']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
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
        } else {
            echo json_encode([
                'success' => true,
                'message' => 'No active fermentation, reading not saved'
            ]);
            exit;
        }
    }
    
    // Insert reading
    $stmt = $pdo->prepare("
        INSERT INTO readings (config_id, temp_fridge, temp_fermenter, temp_target)
        VALUES (?, ?, ?, ?)
    ");
    
    $stmt->execute([
        $config_id,
        $temp_fridge,
        $temp_fermenter,
        $temp_target
    ]);
    
    // ✅ LIMPEZA APÓS INSERT
    DatabaseCleanup::cleanupTable($pdo, 'readings', $config_id);
    
    // Insert iSpindel data if available
    if ($gravity !== null || $spindel_temp !== null) {
        $stmt = $pdo->prepare("
            INSERT INTO ispindel_readings (config_id, temperature, gravity, battery)
            VALUES (?, ?, ?, ?)
        ");
        
        $stmt->execute([
            $config_id,
            $spindel_temp,
            $gravity,
            $spindel_battery
        ]);
        
        // ✅ LIMPEZA APÓS INSERT
        DatabaseCleanup::cleanupTable($pdo, 'ispindel_readings', $config_id);
    }
    
    $reading_id = $pdo->lastInsertId();
    
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