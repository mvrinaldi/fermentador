<?php
// api/maintenance.php - Limpeza emergencial
header('Content-Type: application/json');

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once $_SERVER['DOCUMENT_ROOT'] . '/classes/DatabaseCleanup.php';

$secret = $_GET['secret'] ?? '';

if ($secret !== 'ferment2024cleanup') {
    http_response_code(403);
    echo json_encode(['error' => 'Unauthorized']);
    exit;
}

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    $results = [];
    
    // Buscar todas as configurações ativas
    $stmt = $pdo->prepare("SELECT DISTINCT id FROM configurations WHERE status = 'active'");
    $stmt->execute();
    $configs = $stmt->fetchAll(PDO::FETCH_COLUMN);
    
    // Limpeza agressiva de cada config
    foreach ($configs as $configId) {
        $results["config_{$configId}"] = [
            'readings' => DatabaseCleanup::aggressiveCleanup($pdo, 'readings', $configId, 200),
            'controller_states' => DatabaseCleanup::aggressiveCleanup($pdo, 'controller_states', $configId, 200),
            'esp_heartbeat' => DatabaseCleanup::aggressiveCleanup($pdo, 'esp_heartbeat', $configId, 50),
            'fermentation_states' => DatabaseCleanup::aggressiveCleanup($pdo, 'fermentation_states', $configId, 100),
            'ispindel_readings' => DatabaseCleanup::aggressiveCleanup($pdo, 'ispindel_readings', $configId, 500)
        ];
    }
    
    // Limpar órfãos
    $results['orphans_cleaned'] = DatabaseCleanup::cleanupOrphans($pdo);
    
    echo json_encode([
        'success' => true,
        'results' => $results,
        'timestamp' => date('Y-m-d H:i:s')
    ]);
    
} catch (Exception $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}