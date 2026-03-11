<?php
// limpeza.php - Ferramenta de Limpeza do Banco de Dados
// Acesse: https://fermentador.mvrinaldi.com.br/limpeza.php

ini_set('display_errors', 1);
error_reporting(E_ALL);

// Carregar configurações e classes
require_once $_SERVER['DOCUMENT_ROOT'] . '/config/database.php';
require_once __DIR__ . '/classes/DatabaseCleanup.php';

// Iniciar sessão
session_start();

// Processar logout
if (isset($_GET['logout'])) {
    session_destroy();
    header('Location: ' . $_SERVER['PHP_SELF']);
    exit;
}

// ===== SISTEMA DE LOGIN VIA BANCO DE DADOS =====
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['login'])) {
    try {
        $pdo = new PDO(
            "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
            DB_USER,
            DB_PASS
        );
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        
        $email = $_POST['email'] ?? '';
        $password = $_POST['password'] ?? '';
        
        $stmt = $pdo->prepare("SELECT id, email, password_hash, nome FROM users WHERE email = ? AND is_active = 1");
        $stmt->execute([$email]);
        $user = $stmt->fetch();
        
        if ($user && password_verify($password, $user['password_hash'])) {
            $_SESSION['user_id'] = $user['id'];
            $_SESSION['user_email'] = $user['email'];
            $_SESSION['user_name'] = $user['nome'];
            $_SESSION['login_time'] = time();
            
            // Atualizar último login
            $pdo->prepare("UPDATE users SET last_login = NOW() WHERE id = ?")->execute([$user['id']]);
            
            header('Location: ' . $_SERVER['PHP_SELF']);
            exit;
        } else {
            $login_error = "Email ou senha inválidos";
        }
    } catch (Exception $e) {
        $login_error = "Erro ao conectar: " . $e->getMessage();
    }
}

// Verificar autenticação (expira após 8 horas)
$is_authenticated = false;
if (isset($_SESSION['user_id'])) {
    if (time() - $_SESSION['login_time'] < 28800) { // 8 horas
        $is_authenticated = true;
    } else {
        session_destroy();
    }
}

// Se não estiver autenticado, mostra tela de login
if (!$is_authenticated) {
    ?>
    <!DOCTYPE html>
    <html lang="pt-BR">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Limpeza - Acesso Restrito</title>
        <style>
            * { margin: 0; padding: 0; box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
            body { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; align-items: center; justify-content: center; }
            .login-box { background: white; padding: 40px; border-radius: 16px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); width: 100%; max-width: 400px; animation: fadeIn 0.3s ease; }
            @keyframes fadeIn { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }
            h1 { color: #333; margin-bottom: 10px; font-size: 24px; }
            p { color: #666; margin-bottom: 30px; font-size: 14px; }
            .input-group { margin-bottom: 20px; }
            label { display: block; margin-bottom: 5px; color: #555; font-weight: 500; font-size: 14px; }
            input { width: 100%; padding: 12px 16px; border: 2px solid #e5e7eb; border-radius: 8px; font-size: 16px; transition: all 0.2s; }
            input:focus { outline: none; border-color: #8b5cf6; box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.1); }
            button { width: 100%; padding: 12px; background: #8b5cf6; color: white; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: background 0.2s; }
            button:hover { background: #7c3aed; }
            .error { background: #fee2e2; color: #dc2626; padding: 12px; border-radius: 8px; margin-bottom: 20px; font-size: 14px; border: 1px solid #fecaca; }
        </style>
    </head>
    <body>
        <div class="login-box">
            <h1>🔐 Área Restrita</h1>
            <p>Use seu email e senha do sistema de fermentação.</p>
            
            <?php if (isset($login_error)): ?>
                <div class="error">⚠️ <?php echo htmlspecialchars($login_error); ?></div>
            <?php endif; ?>
            
            <form method="POST">
                <div class="input-group">
                    <label>Email</label>
                    <input type="email" name="email" required autofocus value="mvrinaldi@live.com">
                </div>
                <div class="input-group">
                    <label>Senha</label>
                    <input type="password" name="password" required>
                </div>
                <button type="submit" name="login">Entrar</button>
            </form>
        </div>
    </body>
    </html>
    <?php
    exit;
}

// ========== INÍCIO DO SCRIPT PRINCIPAL ==========

try {
    // Conectar ao banco
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
        DB_USER,
        DB_PASS
    );
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    // Tabelas e limites
    $tableConfig = [
        'readings' => ['keep' => 200, 'timestamp' => 'reading_timestamp', 'label' => 'Leituras'],
        'controller_states' => ['keep' => 200, 'timestamp' => 'state_timestamp', 'label' => 'Estado do Controle'],
        'esp_heartbeat' => ['keep' => 50, 'timestamp' => 'heartbeat_timestamp', 'label' => 'Heartbeat ESP'],
        'fermentation_states' => ['keep' => 100, 'timestamp' => 'state_timestamp', 'label' => 'Estados da Fermentação'],
        'ispindel_readings' => ['keep' => 500, 'timestamp' => 'reading_timestamp', 'label' => 'Leituras iSpindel']
    ];

    // Lista de tabelas monitoradas
    $monitoredTables = array_keys($tableConfig);

    // Processar ação
    $action = $_GET['action'] ?? 'list';
    $configId = isset($_GET['config_id']) ? (int)$_GET['config_id'] : null;
    $message = '';
    $error = '';

    // Função para deletar TODOS os registros de um config_id (limpeza total)
    function deleteAllRecords($pdo, $configId) {
        $tables = ['readings', 'controller_states', 'esp_heartbeat', 'fermentation_states', 'ispindel_readings'];
        $totalDeleted = 0;
        
        foreach ($tables as $table) {
            $stmt = $pdo->prepare("DELETE FROM {$table} WHERE config_id = ?");
            $stmt->execute([$configId]);
            $deleted = $stmt->rowCount();
            $totalDeleted += $deleted;
        }
        
        return $totalDeleted;
    }

    // Executar limpeza TOTAL em um config_id (deleta tudo)
    if ($action === 'delete-all' && $configId && isset($_GET['confirm']) && $_GET['confirm'] === 'yes') {
        try {
            $totalDeleted = deleteAllRecords($pdo, $configId);
            $message = "✅ Limpeza TOTAL concluída para ID {$configId}. Total de registros deletados: {$totalDeleted}";
        } catch (Exception $e) {
            $error = "Erro na limpeza total: " . $e->getMessage();
        }
    }

    // Executar limpeza agressiva (mantém os limites)
    if ($action === 'clean' && $configId && isset($_GET['confirm']) && $_GET['confirm'] === 'yes') {
        try {
            $results = [];
            foreach ($tableConfig as $table => $cfg) {
                $deleted = DatabaseCleanup::aggressiveCleanup($pdo, $table, $configId, $cfg['keep']);
                if ($deleted === false) {
                    $results[$table] = ['status' => 'error', 'deleted' => 0];
                } else {
                    $results[$table] = ['status' => 'success', 'deleted' => $deleted];
                }
            }
            $message = "✅ Limpeza agressiva concluída para configuração ID {$configId}";
        } catch (Exception $e) {
            $error = "Erro na limpeza: " . $e->getMessage();
        }
    }

    // Executar limpeza em TODAS as configurações
    if ($action === 'clean-all' && isset($_GET['confirm']) && $_GET['confirm'] === 'yes') {
        try {
            // Buscar todos os config_id que existem nas tabelas
            $allConfigIds = [];
            foreach ($monitoredTables as $table) {
                $stmt = $pdo->query("SELECT DISTINCT config_id FROM {$table} WHERE config_id IS NOT NULL");
                while ($row = $stmt->fetch(PDO::FETCH_COLUMN)) {
                    $allConfigIds[$row] = true;
                }
            }
            
            $totalDeleted = [];
            foreach (array_keys($allConfigIds) as $cid) {
                foreach ($tableConfig as $table => $cfg) {
                    $deleted = DatabaseCleanup::aggressiveCleanup($pdo, $table, $cid, $cfg['keep']);
                    if ($deleted === false) {
                        $totalDeleted[$table]['error'] = ($totalDeleted[$table]['error'] ?? 0) + 1;
                    } else {
                        $totalDeleted[$table]['deleted'] = ($totalDeleted[$table]['deleted'] ?? 0) + $deleted;
                    }
                }
            }
            
            $message = "✅ Limpeza concluída em " . count($allConfigIds) . " configurações!";
            if (isset($totalDeleted['esp_heartbeat']['deleted'])) {
                $message .= " Heartbeats deletados: " . $totalDeleted['esp_heartbeat']['deleted'];
            }
        } catch (Exception $e) {
            $error = "Erro na limpeza: " . $e->getMessage();
        }
    }

    ?>
    <!DOCTYPE html>
    <html lang="pt-BR">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🧹 Limpeza do Banco de Dados</title>
        <style>
            * { margin: 0; padding: 0; box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
            body { background: #f3f4f6; padding: 20px; }
            .container { max-width: 1400px; margin: 0 auto; }
            
            /* Header */
            .header { background: white; border-radius: 16px; padding: 20px 30px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); display: flex; justify-content: space-between; align-items: center; }
            .header h1 { font-size: 28px; color: #1f2937; display: flex; align-items: center; gap: 10px; }
            .header h1 i { font-size: 32px; }
            .header .user-info { display: flex; align-items: center; gap: 20px; }
            .header .user-info span { color: #6b7280; }
            .logout-btn { padding: 8px 16px; background: #ef4444; color: white; text-decoration: none; border-radius: 8px; font-size: 14px; transition: background 0.2s; }
            .logout-btn:hover { background: #dc2626; }
            
            /* Botão Voltar */
            .back-btn { display: inline-block; padding: 8px 16px; background: #6b7280; color: white; text-decoration: none; border-radius: 8px; font-size: 14px; margin-right: 10px; transition: background 0.2s; }
            .back-btn:hover { background: #4b5563; }
            
            /* Cards de estatísticas */
            .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 30px; }
            .stat-card { background: white; border-radius: 16px; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); border-left: 4px solid; transition: transform 0.2s; }
            .stat-card:hover { transform: translateY(-2px); box-shadow: 0 10px 15px rgba(0,0,0,0.1); }
            .stat-card .label { font-size: 14px; color: #6b7280; margin-bottom: 5px; }
            .stat-card .value { font-size: 28px; font-weight: bold; color: #1f2937; }
            .stat-card .sub { font-size: 12px; color: #9ca3af; margin-top: 5px; }
            
            /* Alertas */
            .alert { padding: 16px 20px; border-radius: 12px; margin-bottom: 20px; display: flex; align-items: center; gap: 10px; }
            .alert-success { background: #d1fae5; border: 1px solid #a7f3d0; color: #065f46; }
            .alert-error { background: #fee2e2; border: 1px solid #fecaca; color: #991b1b; }
            .alert-warning { background: #fffbeb; border: 1px solid #fde68a; color: #92400e; }
            .alert-danger { background: #fee2e2; border: 1px solid #fecaca; color: #991b1b; font-weight: bold; }
            
            /* Tabela */
            .card { background: white; border-radius: 16px; padding: 25px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); margin-bottom: 25px; }
            .card h2 { font-size: 20px; margin-bottom: 20px; color: #374151; display: flex; align-items: center; gap: 8px; }
            .card h2 i { color: #8b5cf6; }
            
            table { width: 100%; border-collapse: collapse; }
            th { text-align: left; padding: 12px 8px; background: #f9fafb; color: #4b5563; font-weight: 600; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; border-bottom: 2px solid #e5e7eb; }
            td { padding: 12px 8px; border-bottom: 1px solid #e5e7eb; color: #1f2937; }
            tr:hover td { background: #f9fafb; }
            
            .badge { display: inline-block; padding: 4px 8px; border-radius: 9999px; font-size: 12px; font-weight: 500; }
            .badge-active { background: #d1fae5; color: #065f46; }
            .badge-paused { background: #fef3c7; color: #92400e; }
            .badge-completed { background: #e5e7eb; color: #4b5563; }
            .badge-orphan { background: #fee2e2; color: #991b1b; }
            
            .warning-sign { color: #dc2626; font-weight: bold; margin-left: 5px; cursor: help; }
            .btn { display: inline-block; padding: 8px 16px; border-radius: 8px; text-decoration: none; font-size: 14px; font-weight: 500; transition: all 0.2s; border: none; cursor: pointer; }
            .btn-primary { background: #8b5cf6; color: white; }
            .btn-primary:hover { background: #7c3aed; }
            .btn-danger { background: #ef4444; color: white; }
            .btn-danger:hover { background: #dc2626; }
            .btn-warning { background: #f59e0b; color: white; }
            .btn-warning:hover { background: #d97706; }
            .btn-secondary { background: #e5e7eb; color: #4b5563; }
            .btn-secondary:hover { background: #d1d5db; }
            .btn-purple { background: #8b5cf6; color: white; }
            .btn-purple:hover { background: #7c3aed; }
            
            .flex { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
            .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
            .mt-4 { margin-top: 20px; }
            .mb-4 { margin-bottom: 20px; }
            .text-center { text-align: center; }
            .text-right { text-align: right; }
            
            @media (max-width: 768px) {
                .grid-2 { grid-template-columns: 1fr; }
                table { font-size: 12px; }
                td, th { padding: 8px 4px; }
            }
        </style>
    </head>
    <body>
        <div class="container">
            
            <!-- Header -->
            <div class="header">
                <div class="flex">
                    <a href="config.html" class="back-btn">← Voltar para configurações</a>
                    <a href="?action=list" class="back-btn">← Voltar à Lista</a>
                    <h1>
                        <span>🧹</span> 
                        Limpeza do Banco de Dados
                    </h1>
                </div>
                <div class="user-info">
                    <span>👤 <?php echo htmlspecialchars($_SESSION['user_name'] ?? $_SESSION['user_email']); ?></span>
                    <a href="?logout=1" class="logout-btn">Sair</a>
                </div>
            </div>

            <?php if ($message): ?>
                <div class="alert alert-success">✅ <?php echo htmlspecialchars($message); ?></div>
            <?php endif; ?>
            
            <?php if ($error): ?>
                <div class="alert alert-error">❌ <?php echo htmlspecialchars($error); ?></div>
            <?php endif; ?>

            <?php
            // Estatísticas GLOBAIS (todos os registros)
            $globalStats = [];
            $totalRecords = 0;
            foreach ($monitoredTables as $table) {
                $stmt = $pdo->query("SELECT COUNT(*) as total FROM {$table}");
                $globalStats[$table] = $stmt->fetchColumn();
                $totalRecords += $globalStats[$table];
            }
            
            // Total de configurações (incluindo órfãos)
            $allConfigIds = [];
            foreach ($monitoredTables as $table) {
                $stmt = $pdo->query("SELECT DISTINCT config_id FROM {$table} WHERE config_id IS NOT NULL");
                while ($row = $stmt->fetch(PDO::FETCH_COLUMN)) {
                    $allConfigIds[$row] = true;
                }
            }
            $totalConfigsWithData = count($allConfigIds);
            
            // Configuração com mais heartbeats
            $stmt = $pdo->query("
                SELECT config_id, COUNT(*) as total 
                FROM esp_heartbeat 
                GROUP BY config_id 
                ORDER BY total DESC 
                LIMIT 1
            ");
            $topHeartbeat = $stmt->fetch();
            ?>

            <!-- Cards de estatísticas globais -->
            <div class="stats-grid">
                <div class="stat-card" style="border-left-color: #8b5cf6;">
                    <div class="label">Configurações com Dados</div>
                    <div class="value"><?php echo number_format($totalConfigsWithData, 0, ',', '.'); ?></div>
                    <div class="sub">IDs que possuem registros</div>
                </div>
                <div class="stat-card" style="border-left-color: #10b981;">
                    <div class="label">Total de Registros</div>
                    <div class="value"><?php echo number_format($totalRecords, 0, ',', '.'); ?></div>
                    <div class="sub">em todas as tabelas</div>
                </div>
                <div class="stat-card" style="border-left-color: #f59e0b;">
                    <div class="label">Total Heartbeats</div>
                    <div class="value"><?php echo number_format($globalStats['esp_heartbeat'] ?? 0, 0, ',', '.'); ?></div>
                    <div class="sub">Config #<?php echo $topHeartbeat['config_id'] ?? '?'; ?>: <?php echo number_format($topHeartbeat['total'] ?? 0); ?></div>
                </div>
                <div class="stat-card" style="border-left-color: #ef4444;">
                    <div class="label">Ação Rápida</div>
                    <div class="value">
                        <a href="?action=clean-all&confirm=yes" class="btn btn-warning" style="font-size: 14px; padding: 5px 10px;" onclick="return confirm('ATENÇÃO! Isso vai limpar TODAS as configurações com registros (mantendo os limites). Continuar?');">
                            🧹 Limpar TODOS (mantendo limites)
                        </a>
                    </div>
                    <div class="sub"><?php echo $totalConfigsWithData; ?> IDs com dados</div>
                </div>
            </div>

            <?php if ($action === 'list' || !$configId): ?>
                
                <!-- Lista de todas as configurações com registros -->
                <div class="card">
                    <h2>📋 Configurações com Registros no Banco</h2>
                    
                    <?php
                    // Buscar TODOS os config_id que aparecem nas tabelas monitoradas
                    $allConfigIds = [];
                    foreach ($monitoredTables as $table) {
                        $stmt = $pdo->query("SELECT DISTINCT config_id FROM {$table} WHERE config_id IS NOT NULL");
                        while ($row = $stmt->fetch(PDO::FETCH_COLUMN)) {
                            $allConfigIds[$row] = true;
                        }
                    }
                    
                    $configs = [];
                    if (!empty($allConfigIds)) {
                        $ids = implode(',', array_keys($allConfigIds));
                        
                        // Buscar informações das configurações que existem na tabela configurations
                        $stmt = $pdo->query("
                            SELECT c.*,
                                   (SELECT COUNT(*) FROM readings WHERE config_id = c.id) as readings_count,
                                   (SELECT COUNT(*) FROM controller_states WHERE config_id = c.id) as controller_count,
                                   (SELECT COUNT(*) FROM esp_heartbeat WHERE config_id = c.id) as heartbeat_count,
                                   (SELECT COUNT(*) FROM fermentation_states WHERE config_id = c.id) as fermentation_count,
                                   (SELECT COUNT(*) FROM ispindel_readings WHERE config_id = c.id) as ispindel_count
                            FROM configurations c
                            WHERE c.id IN ({$ids})
                        ");
                        $existingConfigs = $stmt->fetchAll();
                        
                        // Mapear por ID
                        $configMap = [];
                        foreach ($existingConfigs as $cfg) {
                            $configMap[$cfg['id']] = $cfg;
                        }
                        
                        // Para cada ID encontrado, criar entrada
                        foreach (array_keys($allConfigIds) as $id) {
                            if (isset($configMap[$id])) {
                                // Configuração existe na tabela
                                $configs[] = $configMap[$id];
                            } else {
                                // ID órfão - buscar contagens diretamente
                                $orphanData = [
                                    'id' => $id,
                                    'name' => '⚠️ CONFIGURAÇÃO ÓRFÃ',
                                    'status' => 'orphan',
                                    'created_at' => null,
                                    'readings_count' => 0,
                                    'controller_count' => 0,
                                    'heartbeat_count' => 0,
                                    'fermentation_count' => 0,
                                    'ispindel_count' => 0
                                ];
                                
                                // Contar registros órfãos
                                foreach ($monitoredTables as $table) {
                                    $stmt = $pdo->prepare("SELECT COUNT(*) FROM {$table} WHERE config_id = ?");
                                    $stmt->execute([$id]);
                                    $count = $stmt->fetchColumn();
                                    
                                    switch ($table) {
                                        case 'readings': $orphanData['readings_count'] = $count; break;
                                        case 'controller_states': $orphanData['controller_count'] = $count; break;
                                        case 'esp_heartbeat': $orphanData['heartbeat_count'] = $count; break;
                                        case 'fermentation_states': $orphanData['fermentation_count'] = $count; break;
                                        case 'ispindel_readings': $orphanData['ispindel_count'] = $count; break;
                                    }
                                }
                                
                                $configs[] = $orphanData;
                            }
                        }
                        
                        // Ordenar: ativos primeiro, depois pausados, depois órfãos, depois completed
                        usort($configs, function($a, $b) {
                            $order = ['active' => 1, 'paused' => 2, 'orphan' => 3, 'completed' => 4];
                            $aOrder = $order[$a['status']] ?? 5;
                            $bOrder = $order[$b['status']] ?? 5;
                            return $aOrder <=> $bOrder;
                        });
                    }
                    ?>
                    
                    <?php if (empty($configs)): ?>
                        <p style="color: #6b7280; text-align: center; padding: 40px;">Nenhum registro encontrado em nenhuma tabela.</p>
                    <?php else: ?>
                        <table>
                            <thead>
                                <tr>
                                    <th>ID</th>
                                    <th>Nome</th>
                                    <th>Status</th>
                                    <th>Leituras</th>
                                    <th>Controle</th>
                                    <th>Heartbeat</th>
                                    <th>Fermentação</th>
                                    <th>iSpindel</th>
                                    <th>Criação</th>
                                    <th colspan="2">Ações</th>
                                </tr>
                            </thead>
                            <tbody>
                                <?php foreach ($configs as $config): 
                                    $isOrphan = $config['status'] === 'orphan';
                                    
                                    $statusClass = match($config['status']) {
                                        'active' => 'badge-active',
                                        'paused' => 'badge-paused',
                                        'orphan' => 'badge-orphan',
                                        default => 'badge-completed'
                                    };
                                    
                                    $statusDisplay = match($config['status']) {
                                        'active' => '🟢 ATIVO',
                                        'paused' => '🟡 PAUSADO',
                                        'orphan' => '❌ ÓRFÃO',
                                        default => '⚪ ' . $config['status']
                                    };
                                    
                                    $hasHighReadings = $config['readings_count'] > 500;
                                    $hasHighController = $config['controller_count'] > 500;
                                    $hasHighHeartbeat = $config['heartbeat_count'] > 200;
                                    $hasHighFermentation = $config['fermentation_count'] > 200;
                                    $hasHighIspindel = $config['ispindel_count'] > 1000;
                                    $hasProblems = $hasHighReadings || $hasHighController || $hasHighHeartbeat || $hasHighFermentation || $hasHighIspindel;
                                ?>
                                <tr style="<?php echo $isOrphan ? 'background-color: #fff1f0;' : ''; ?>">
                                    <td><strong>#<?php echo $config['id']; ?></strong></td>
                                    <td><?php echo htmlspecialchars($config['name']); ?></td>
                                    <td><span class="badge <?php echo $statusClass; ?>"><?php echo $statusDisplay; ?></span></td>
                                    <td>
                                        <?php echo number_format($config['readings_count'], 0, ',', '.'); ?>
                                        <?php if ($hasHighReadings): ?><span class="warning-sign" title="Acima de 500 registros">⚠️</span><?php endif; ?>
                                    </td>
                                    <td>
                                        <?php echo number_format($config['controller_count'], 0, ',', '.'); ?>
                                        <?php if ($hasHighController): ?><span class="warning-sign" title="Acima de 500 registros">⚠️</span><?php endif; ?>
                                    </td>
                                    <td>
                                        <?php echo number_format($config['heartbeat_count'], 0, ',', '.'); ?>
                                        <?php if ($hasHighHeartbeat): ?><span class="warning-sign" title="Acima de 200 registros">⚠️</span><?php endif; ?>
                                    </td>
                                    <td>
                                        <?php echo number_format($config['fermentation_count'], 0, ',', '.'); ?>
                                        <?php if ($hasHighFermentation): ?><span class="warning-sign" title="Acima de 200 registros">⚠️</span><?php endif; ?>
                                    </td>
                                    <td>
                                        <?php echo number_format($config['ispindel_count'], 0, ',', '.'); ?>
                                        <?php if ($hasHighIspindel): ?><span class="warning-sign" title="Acima de 1000 registros">⚠️</span><?php endif; ?>
                                    </td>
                                    <td><?php echo $config['created_at'] ? date('d/m/Y', strtotime($config['created_at'])) : '-'; ?></td>
                                    <td>
                                        <a href="?action=view&config_id=<?php echo $config['id']; ?>" class="btn btn-secondary" style="padding: 4px 8px; font-size: 12px;">👁️ Ver</a>
                                    </td>
                                    <td>
                                        <div class="flex" style="gap: 5px;">
                                            <?php if ($isOrphan): ?>
                                                <a href="?action=delete-all&config_id=<?php echo $config['id']; ?>" class="btn btn-danger" style="padding: 4px 8px; font-size: 12px;" onclick="return confirm('ATENÇÃO! Isso vai DELETAR TODOS OS REGISTROS do ID <?php echo $config['id']; ?> (órfão). Esta ação é irreversível. Continuar?');">
                                                    🗑️ Deletar Tudo
                                                </a>
                                            <?php else: ?>
                                                <a href="?action=clean&config_id=<?php echo $config['id']; ?>" class="btn btn-warning" style="padding: 4px 8px; font-size: 12px; <?php echo !$hasProblems ? 'opacity:0.5;' : ''; ?>" <?php echo !$hasProblems ? 'onclick="return confirm(\'Esta configuração não tem muitos registros. Deseja limpar mesmo assim (mantendo os limites)?\');"' : ''; ?>>
                                                    🧹 Limpar (mantendo limites)
                                                </a>
                                                <a href="?action=delete-all&config_id=<?php echo $config['id']; ?>" class="btn btn-danger" style="padding: 4px 8px; font-size: 12px;" onclick="return confirm('ATENÇÃO! Isso vai DELETAR TODOS OS REGISTROS da configuração <?php echo addslashes($config['name']); ?>. Esta ação é irreversível. Continuar?');">
                                                    🗑️ Deletar Tudo
                                                </a>
                                            <?php endif; ?>
                                        </div>
                                    </td>
                                </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                        
                        <div class="mt-4" style="color: #6b7280; font-size: 13px;">
                            ⚠️ Ícones ⚠️ indicam tabelas com muitos registros que precisam de limpeza.<br>
                            🔴 Linhas vermelhas são IDs órfãos (configuração foi deletada mas registros ainda existem no banco).<br>
                            🧹 <strong>Limpar (agressivo)</strong> = mantém os últimos registros conforme limites configurados.<br>
                            🗑️ <strong>Deletar Tudo</strong> = remove TODOS os registros deste ID (limpeza total).
                        </div>
                    <?php endif; ?>
                </div>
                
            <?php elseif ($action === 'view' && $configId): ?>
                
                <!-- Detalhes da configuração -->
                <?php
                // Verificar se o ID existe em alguma tabela
                $totalRecords = 0;
                foreach ($monitoredTables as $table) {
                    $stmt = $pdo->prepare("SELECT COUNT(*) FROM {$table} WHERE config_id = ?");
                    $stmt->execute([$configId]);
                    $totalRecords += $stmt->fetchColumn();
                }
                
                if ($totalRecords === 0) {
                    echo '<div class="alert alert-error">Nenhum registro encontrado para o ID ' . $configId . '</div>';
                } else {
                    // Buscar dados da configuração se existir
                    $stmt = $pdo->prepare("SELECT * FROM configurations WHERE id = ?");
                    $stmt->execute([$configId]);
                    $config = $stmt->fetch();
                    $isOrphan = !$config;
                    
                    if ($isOrphan) {
                        $config = ['id' => $configId, 'name' => 'CONFIGURAÇÃO ÓRFÃ', 'status' => 'orphan', 'created_at' => null, 'started_at' => null, 'current_stage_index' => 0, 'times_used' => 0];
                    }
                    
                    // Buscar contagens atuais
                    $counts = [];
                    foreach ($tableConfig as $table => $cfg) {
                        $stmt = $pdo->prepare("SELECT COUNT(*) as total, MIN({$cfg['timestamp']}) as oldest, MAX({$cfg['timestamp']}) as newest FROM {$table} WHERE config_id = ?");
                        $stmt->execute([$configId]);
                        $counts[$table] = $stmt->fetch();
                    }
                ?>
                
                <div class="grid-2">
                    <!-- Informações da configuração -->
                    <div class="card">
                        <h2>📌 <?php echo $isOrphan ? 'ID Órfão' : 'Configuração'; ?> #<?php echo $configId; ?>: <?php echo htmlspecialchars($config['name']); ?></h2>
                        
                        <table style="width: auto;">
                            <tr><td><strong>Status:</strong></td><td>
                                <?php if ($isOrphan): ?>
                                    <span class="badge badge-orphan">❌ ÓRFÃO</span>
                                <?php else: ?>
                                    <span class="badge <?php echo $config['status'] === 'active' ? 'badge-active' : ($config['status'] === 'paused' ? 'badge-paused' : 'badge-completed'); ?>">
                                        <?php echo $config['status']; ?>
                                    </span>
                                <?php endif; ?>
                            </td></tr>
                            <?php if (!$isOrphan && $config['created_at']): ?>
                                <tr><td><strong>Criada em:</strong></td><td><?php echo date('d/m/Y H:i', strtotime($config['created_at'])); ?></td></tr>
                            <?php endif; ?>
                            <?php if (!$isOrphan && $config['started_at']): ?>
                                <tr><td><strong>Iniciada em:</strong></td><td><?php echo date('d/m/Y H:i', strtotime($config['started_at'])); ?></td></tr>
                            <?php endif; ?>
                            <?php if (!$isOrphan): ?>
                                <tr><td><strong>Índice atual:</strong></td><td><?php echo $config['current_stage_index']; ?></td></tr>
                                <tr><td><strong>Vezes usada:</strong></td><td><?php echo $config['times_used']; ?></td></tr>
                            <?php endif; ?>
                        </table>
                    </div>
                    
                    <!-- Resumo da limpeza -->
                    <div class="card">
                        <h2>📊 Resumo da Limpeza</h2>
                        <table>
                            <tr>
                                <th>Tabela</th>
                                <th>Registros</th>
                                <th>Limite</th>
                                <th>Status</th>
                                <th>Mais antigo</th>
                            </tr>
                            <?php foreach ($tableConfig as $table => $cfg): 
                                $count = $counts[$table]['total'];
                                $limit = $cfg['keep'];
                                $status = $count > $limit ? '⚠️ Acima' : '✅ OK';
                                $statusColor = $count > $limit ? '#ef4444' : '#10b981';
                            ?>
                            <tr>
                                <td><?php echo $cfg['label']; ?></td>
                                <td><strong><?php echo number_format($count, 0, ',', '.'); ?></strong></td>
                                <td><?php echo $limit; ?></td>
                                <td style="color: <?php echo $statusColor; ?>; font-weight: 500;"><?php echo $status; ?></td>
                                <td><?php echo $counts[$table]['oldest'] ? date('d/m/Y H:i', strtotime($counts[$table]['oldest'])) : '-'; ?></td>
                            </tr>
                            <?php endforeach; ?>
                        </table>
                        
                        <div class="flex mt-4">
                            <a href="?action=clean&config_id=<?php echo $configId; ?>" class="btn btn-warning" onclick="return confirm('Executar limpeza agressiva (manter limites)?')">🧹 Limpar (agressivo)</a>
                            <a href="?action=delete-all&config_id=<?php echo $configId; ?>" class="btn btn-danger" onclick="return confirm('ATENÇÃO! Isso vai DELETAR TODOS OS REGISTROS deste ID. Esta ação é irreversível. Continuar?')">🗑️ Deletar Tudo</a>
                            <a href="?action=list" class="btn btn-secondary">← Voltar</a>
                        </div>
                    </div>
                </div>
                
                <!-- Últimos heartbeats (apenas deste config_id) -->
                <div class="card">
                    <h2>📈 Últimos Heartbeats (ID #<?php echo $configId; ?>)</h2>
                    <?php
                    $stmt = $pdo->prepare("SELECT * FROM esp_heartbeat WHERE config_id = ? ORDER BY heartbeat_timestamp DESC LIMIT 10");
                    $stmt->execute([$configId]);
                    $heartbeats = $stmt->fetchAll();
                    
                    if ($heartbeats):
                    ?>
                    <table>
                        <thead>
                            <tr>
                                <th>ID</th>
                                <th>Timestamp</th>
                                <th>Uptime (s)</th>
                                <th>Free Heap</th>
                                <th>Temp Ferm</th>
                                <th>Temp Fridge</th>
                                <th>Control Status</th>
                            </tr>
                        </thead>
                        <tbody>
                            <?php foreach ($heartbeats as $hb): ?>
                            <tr>
                                <td>#<?php echo $hb['id']; ?></td>
                                <td><?php echo date('d/m/Y H:i:s', strtotime($hb['heartbeat_timestamp'])); ?></td>
                                <td><?php echo number_format($hb['uptime_seconds']); ?></td>
                                <td><?php echo number_format($hb['free_heap']); ?> bytes</td>
                                <td><?php echo $hb['temp_fermenter']; ?>°C</td>
                                <td><?php echo $hb['temp_fridge']; ?>°C</td>
                                <td><?php 
                                    $cs = json_decode($hb['control_status'], true);
                                    echo $cs['state'] ?? $cs['s'] ?? '-';
                                ?></td>
                            </tr>
                            <?php endforeach; ?>
                        </tbody>
                    </table>
                    <?php else: ?>
                    <p style="color: #6b7280;">Nenhum heartbeat encontrado para este ID.</p>
                    <?php endif; ?>
                </div>
                
                <?php } ?>
                
            <?php elseif ($action === 'clean' && $configId): ?>
                
                <!-- Confirmação de limpeza agressiva (mantém limites) -->
                <div class="card">
                    <h2>⚠️ Confirmar Limpeza Agressiva</h2>
                    
                    <?php
                    // Verificar se é órfão
                    $stmt = $pdo->prepare("SELECT id FROM configurations WHERE id = ?");
                    $stmt->execute([$configId]);
                    $configExists = $stmt->fetch();
                    $isOrphan = !$configExists;
                    
                    if ($isOrphan) {
                        echo "<p>Você está prestes a limpar registros de um <strong>ID ÓRFÃO</strong>:</p>";
                    } else {
                        $stmt = $pdo->prepare("SELECT name FROM configurations WHERE id = ?");
                        $stmt->execute([$configId]);
                        $configName = $stmt->fetchColumn();
                        echo "<p>Você está prestes a limpar registros da configuração:</p>";
                    }
                    
                    echo "<p style='font-size: 18px; margin: 15px 0;'><strong>ID #{$configId}" . ($isOrphan ? '' : ': ' . htmlspecialchars($configName)) . "</strong></p>";
                    
                    // Mostrar contagens atuais
                    $totalRecords = 0;
                    echo "<p>Esta operação vai <strong>manter apenas os registros mais recentes</strong> conforme os limites:</p>";
                    echo "<table style='width: auto; margin: 20px 0;'>";
                    echo "<tr><th>Tabela</th><th>Registros</th><th>Manter</th><th>Deletar</th></tr>";
                    
                    foreach ($tableConfig as $table => $cfg):
                        $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM {$table} WHERE config_id = ?");
                        $stmt->execute([$configId]);
                        $count = $stmt->fetchColumn();
                        $totalRecords += $count;
                        
                        $toDelete = max(0, $count - $cfg['keep']);
                    ?>
                    <tr>
                        <td><?php echo $cfg['label']; ?></td>
                        <td><?php echo number_format($count, 0, ',', '.'); ?></td>
                        <td><?php echo $cfg['keep']; ?></td>
                        <td><strong style="color: <?php echo $toDelete > 0 ? '#ef4444' : '#10b981'; ?>"><?php echo number_format($toDelete, 0, ',', '.'); ?></strong></td>
                    </tr>
                    <?php endforeach; ?>
                    
                    <tr style="border-top: 2px solid #e5e7eb;">
                        <td><strong>TOTAL</strong></td>
                        <td><strong><?php echo number_format($totalRecords, 0, ',', '.'); ?></strong></td>
                        <td></td>
                        <td></td>
                    </tr>
                    </table>
                    
                    <?php if ($totalRecords > 0): ?>
                    <div class="alert alert-warning">
                        <strong>⚠️ Esta ação manterá apenas os últimos registros!</strong> Os registros antigos serão deletados permanentemente.
                    </div>
                    
                    <div class="flex">
                        <a href="?action=clean&config_id=<?php echo $configId; ?>&confirm=yes" class="btn btn-warning" style="padding: 12px 24px;">✅ SIM, EXECUTAR LIMPEZA AGRESSIVA</a>
                        <a href="?action=view&config_id=<?php echo $configId; ?>" class="btn btn-secondary" style="padding: 12px 24px;">↩️ Não, voltar</a>
                    </div>
                    <?php else: ?>
                    <div class="alert alert-success">
                        ✅ Não há registros para limpar neste ID.
                    </div>
                    <a href="?action=list" class="btn btn-secondary">← Voltar</a>
                    <?php endif; ?>
                </div>
                
            <?php elseif ($action === 'delete-all' && $configId): ?>
                
                <!-- Confirmação de limpeza TOTAL (deleta tudo) -->
                <div class="card">
                    <h2>⚠️⚠️⚠️ CONFIRMAR LIMPEZA TOTAL ⚠️⚠️⚠️</h2>
                    
                    <?php
                    // Verificar se é órfão
                    $stmt = $pdo->prepare("SELECT id FROM configurations WHERE id = ?");
                    $stmt->execute([$configId]);
                    $configExists = $stmt->fetch();
                    $isOrphan = !$configExists;
                    
                    if ($isOrphan) {
                        echo "<p>Você está prestes a DELETAR TODOS OS REGISTROS de um <strong>ID ÓRFÃO</strong>:</p>";
                    } else {
                        $stmt = $pdo->prepare("SELECT name FROM configurations WHERE id = ?");
                        $stmt->execute([$configId]);
                        $configName = $stmt->fetchColumn();
                        echo "<p>Você está prestes a DELETAR TODOS OS REGISTROS da configuração:</p>";
                    }
                    
                    echo "<p style='font-size: 18px; margin: 15px 0;'><strong>ID #{$configId}" . ($isOrphan ? '' : ': ' . htmlspecialchars($configName)) . "</strong></p>";
                    
                    // Mostrar contagens atuais
                    $totalRecords = 0;
                    echo "<p>Esta operação vai <strong>DELETAR TODOS OS REGISTROS</strong> das seguintes tabelas:</p>";
                    echo "<table style='width: auto; margin: 20px 0;'>";
                    echo "<tr><th>Tabela</th><th>Registros a serem deletados</th></tr>";
                    
                    foreach ($tableConfig as $table => $cfg):
                        $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM {$table} WHERE config_id = ?");
                        $stmt->execute([$configId]);
                        $count = $stmt->fetchColumn();
                        $totalRecords += $count;
                    ?>
                    <tr>
                        <td><?php echo $cfg['label']; ?></td>
                        <td><strong style="color: #ef4444;"><?php echo number_format($count, 0, ',', '.'); ?></strong></td>
                    </tr>
                    <?php endforeach; ?>
                    
                    <tr style="border-top: 2px solid #e5e7eb;">
                        <td><strong>TOTAL</strong></td>
                        <td><strong style="color: #ef4444;"><?php echo number_format($totalRecords, 0, ',', '.'); ?></strong></td>
                    </tr>
                    </table>
                    
                    <?php if ($totalRecords > 0): ?>
                    <div class="alert alert-danger">
                        <strong>⚠️⚠️⚠️ ATENÇÃO! ESTA AÇÃO É IRREVERSÍVEL! ⚠️⚠️⚠️</strong><br>
                        Todos os registros acima serão PERMANENTEMENTE DELETADOS do banco de dados.
                        <?php if (!$isOrphan): ?>
                            <br>A configuração em si NÃO será deletada, apenas seus dados históricos.
                        <?php endif; ?>
                    </div>
                    
                    <div class="flex">
                        <a href="?action=delete-all&config_id=<?php echo $configId; ?>&confirm=yes" class="btn btn-danger" style="padding: 12px 24px; font-weight: bold;">✅ SIM, DELETAR TUDO</a>
                        <a href="?action=view&config_id=<?php echo $configId; ?>" class="btn btn-secondary" style="padding: 12px 24px;">↩️ Não, voltar</a>
                    </div>
                    <?php else: ?>
                    <div class="alert alert-success">
                        ✅ Não há registros para deletar neste ID.
                    </div>
                    <a href="?action=list" class="btn btn-secondary">← Voltar</a>
                    <?php endif; ?>
                </div>
                
            <?php endif; ?>
            
            <!-- Rodapé -->
            <div style="text-align: center; margin-top: 30px; color: #9ca3af; font-size: 12px;">
                <p>Ferramenta de Limpeza do Banco de Dados v4.0 | Usuário: <?php echo htmlspecialchars($_SESSION['user_email']); ?></p>
                <p>⚠️ Use com responsabilidade - Existem dois tipos de limpeza:</p>
                <p>🧹 <strong>Limpar (agressivo)</strong> = mantém os últimos registros conforme limites configurados.</p>
                <p>🗑️ <strong>Deletar Tudo</strong> = remove TODOS os registros do ID (limpeza total).</p>
                <p>📊 Heartbeats globais: <?php echo number_format($globalStats['esp_heartbeat'] ?? 0, 0, ',', '.'); ?> registros no total</p>
            </div>
            
        </div>
    </body>
    </html>
    <?php

} catch (Exception $e) {
    echo "<div style='background:#fee2e2; color:#991b1b; padding:20px; margin:20px; border-radius:8px;'>";
    echo "<strong>Erro:</strong> " . htmlspecialchars($e->getMessage());
    echo "</div>";
}
?>