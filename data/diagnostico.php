<?php
// diagnostico.php - Script para verificar o estado do banco de dados
header('Content-Type: application/json');

require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';

try {
    $pdo = new PDO("mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4", 
                   DB_USER, DB_PASS);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    $diagnostics = [];
    
    // 1. VERIFICAR FERMENTAÇÕES ATIVAS
    $stmt = $pdo->query("
        SELECT id, name, status, started_at, completed_at 
        FROM configurations 
        ORDER BY started_at DESC 
        LIMIT 5
    ");
    $diagnostics['configurations'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // 2. VERIFICAR ÚLTIMA LEITURA DE TEMPERATURA
    $stmt = $pdo->query("
        SELECT r.*, c.name as config_name
        FROM readings r
        LEFT JOIN configurations c ON r.config_id = c.id
        ORDER BY r.reading_timestamp DESC 
        LIMIT 10
    ");
    $diagnostics['latest_readings'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // 3. CONTAR READINGS POR CONFIG
    $stmt = $pdo->query("
        SELECT 
            config_id, 
            COUNT(*) as total_readings,
            MIN(reading_timestamp) as first_reading,
            MAX(reading_timestamp) as last_reading
        FROM readings 
        GROUP BY config_id
        ORDER BY last_reading DESC
    ");
    $diagnostics['readings_summary'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // 4. VERIFICAR ÚLTIMA LEITURA DO ISPINDEL
    $stmt = $pdo->query("
        SELECT * FROM ispindel_readings 
        ORDER BY reading_timestamp DESC 
        LIMIT 5
    ");
    $diagnostics['latest_ispindel'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // 5. VERIFICAR ESTRUTURA DA TABELA READINGS
    $stmt = $pdo->query("DESCRIBE readings");
    $diagnostics['readings_structure'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    // 6. VERIFICAR SE HÁ DADOS NAS ÚLTIMAS 24H
    $stmt = $pdo->query("
        SELECT COUNT(*) as count_24h
        FROM readings 
        WHERE reading_timestamp >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
    ");
    $result = $stmt->fetch(PDO::FETCH_ASSOC);
    $diagnostics['readings_last_24h'] = $result['count_24h'];
    
    // 7. VERIFICAR HEARTBEAT
    $stmt = $pdo->query("
        SELECT * FROM esp_heartbeat 
        ORDER BY heartbeat_timestamp DESC 
        LIMIT 3
    ");
    $diagnostics['latest_heartbeats'] = $stmt->fetchAll(PDO::FETCH_ASSOC);
    
    echo json_encode($diagnostics, JSON_PRETTY_PRINT);
    
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode([
        'error' => 'Database error',
        'details' => $e->getMessage()
    ], JSON_PRETTY_PRINT);
}