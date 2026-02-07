<?php
/**
 * TESTE DE DIAGNÓSTICO - Alertas de Conclusão de Etapa
 * 
 * Este script simula o disparo de um alerta de etapa concluída
 * para diagnosticar onde está o problema.
 */

error_reporting(E_ALL);
ini_set('display_errors', 1);

echo "=== TESTE DE ALERTA DE ETAPA CONCLUÍDA ===\n\n";

// 1. Carregar configuração do banco
echo "1. Carregando config do banco...\n";
require_once __DIR__ . '/config/database.php';

try {
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
        DB_USER,
        DB_PASS
    );
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    echo "   ✅ Conexão com banco OK\n\n";
} catch (PDOException $e) {
    die("   ❌ ERRO: " . $e->getMessage() . "\n");
}

// 2. Verificar se AlertSystem existe
echo "2. Verificando AlertSystem.php...\n";

$alertSystemPaths = [
    __DIR__ . '/classes/AlertSystem.php',
    __DIR__ . '/AlertSystem.php',
    __DIR__ . '/../classes/AlertSystem.php'
];

$alertSystemFound = null;
foreach ($alertSystemPaths as $path) {
    echo "   Tentando: $path\n";
    if (file_exists($path)) {
        $alertSystemFound = $path;
        echo "   ✅ ENCONTRADO: $path\n";
        break;
    }
}

if (!$alertSystemFound) {
    die("   ❌ ERRO: AlertSystem.php não encontrado!\n");
}

require_once $alertSystemFound;
echo "   ✅ AlertSystem.php carregado\n\n";

// 3. Verificar AlertIntegration
echo "3. Verificando AlertIntegration.php...\n";

$integrationPaths = [
    __DIR__ . '/api/AlertIntegration.php',
    __DIR__ . '/AlertIntegration.php'
];

$integrationFound = null;
foreach ($integrationPaths as $path) {
    echo "   Tentando: $path\n";
    if (file_exists($path)) {
        $integrationFound = $path;
        echo "   ✅ ENCONTRADO: $path\n";
        break;
    }
}

if (!$integrationFound) {
    die("   ❌ ERRO: AlertIntegration.php não encontrado!\n");
}

require_once $integrationFound;
echo "   ✅ AlertIntegration.php carregado\n\n";

// 4. Buscar config_id ativo
echo "4. Buscando fermentação ativa...\n";
$stmt = $pdo->query("SELECT id, name FROM configurations WHERE status = 'active' LIMIT 1");
$config = $stmt->fetch(PDO::FETCH_ASSOC);

if (!$config) {
    die("   ❌ ERRO: Nenhuma fermentação ativa encontrada\n");
}

echo "   ✅ Config ativa: ID={$config['id']}, Nome={$config['name']}\n\n";

// 5. Verificar configuração de alertas
echo "5. Verificando configuração de alertas...\n";
$stmt = $pdo->query("SELECT config_key, config_value FROM system_config WHERE config_key LIKE 'alert_%'");
$alertConfig = [];
while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
    $key = str_replace('alert_', '', $row['config_key']);
    $alertConfig[$key] = $row['config_value'];
    
    // Ocultar tokens/api keys
    $displayValue = $row['config_value'];
    if (strpos($key, 'token') !== false || strpos($key, 'apikey') !== false) {
        if (strlen($displayValue) > 8) {
            $displayValue = substr($displayValue, 0, 4) . '****' . substr($displayValue, -4);
        }
    }
    
    echo "   - {$row['config_key']}: $displayValue\n";
}

$enabled = ($alertConfig['enabled'] ?? '0') === '1';
$minLevel = $alertConfig['min_level'] ?? 'warning';
$telegramToken = $alertConfig['telegram_bot_token'] ?? '';
$telegramChatId = $alertConfig['telegram_chat_id'] ?? '';

echo "\n";
echo "   Sistema ativado: " . ($enabled ? '✅ SIM' : '❌ NÃO') . "\n";
echo "   Nível mínimo: $minLevel " . ($minLevel === 'info' ? '✅' : '⚠️ (deveria ser "info")') . "\n";
echo "   Telegram configurado: " . (!empty($telegramToken) && !empty($telegramChatId) ? '✅ SIM' : '❌ NÃO') . "\n\n";

if (!$enabled) {
    die("   ❌ ERRO FATAL: Sistema de alertas está DESATIVADO!\n");
}

if ($minLevel !== 'info') {
    echo "   ⚠️ AVISO: Nível mínimo é '$minLevel', mas alertas de etapa são 'info'\n";
    echo "             Alertas de etapa NÃO serão enviados!\n\n";
}

// 6. Testar disparo de alerta
echo "6. Disparando alerta de teste...\n";

try {
    $result = AlertIntegration::onStageCompleted(
        $pdo,
        $config['id'],
        1,  // Etapa 1 (índice)
        'Etapa 2 (temperature)',
        'Etapa 3 (ramp)'
    );
    
    if ($result) {
        echo "   ✅ Alerta disparado com sucesso!\n";
    } else {
        echo "   ⚠️ Alerta retornou NULL (pode ter sido bloqueado)\n";
    }
} catch (Exception $e) {
    echo "   ❌ ERRO ao disparar alerta: " . $e->getMessage() . "\n";
}

echo "\n";

// 7. Verificar se alerta foi salvo no banco
echo "7. Verificando alertas no banco...\n";
$stmt = $pdo->prepare("
    SELECT * FROM alerts 
    WHERE config_id = ? 
    AND alert_type = 'stage_completion'
    ORDER BY created_at DESC 
    LIMIT 3
");
$stmt->execute([$config['id']]);
$alerts = $stmt->fetchAll(PDO::FETCH_ASSOC);

if (count($alerts) > 0) {
    echo "   ✅ Encontrados " . count($alerts) . " alertas de conclusão:\n";
    foreach ($alerts as $alert) {
        $readStatus = $alert['is_read'] ? '📖' : '📩';
        echo "      $readStatus ID={$alert['id']}: {$alert['message']} ({$alert['created_at']})\n";
    }
} else {
    echo "   ⚠️ NENHUM alerta de conclusão encontrado no banco!\n";
    echo "      Isso significa que AlertSystem->createStageCompletedAlert() não está sendo executado.\n";
}

echo "\n=== FIM DO TESTE ===\n";