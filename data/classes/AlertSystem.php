<?php
/**
 * Sistema de Alertas para Controle de Fermentação
 * 
 * Monitora condições críticas e envia notificações via WhatsApp/Telegram
 * 
 * @author Marcos Rinaldi
 * @version 1.3 - SIMPLIFICADO: remove min_level, todos alertas são enviados
 * @date Fevereiro 2026
 */

class AlertSystem {
    
    private $pdo;
    private $config;
    
    // Tipos de alerta
    const ALERT_TEMPERATURE = 'temperature';
    const ALERT_ESP_OFFLINE = 'device';
    const ALERT_STAGE_COMPLETE = 'stage_completion';
    const ALERT_FERMENTATION_COMPLETE = 'stage_completion';
    const ALERT_GRAVITY = 'gravity';
    const ALERT_ISPINDEL_OFFLINE = 'device';
    const ALERT_BATTERY_LOW = 'device';
    const ALERT_MEMORY_LOW = 'device';
    const ALERT_ERROR = 'error';
    
    // Níveis de alerta (mantidos para classificação visual, mas NÃO filtram envio)
    const LEVEL_INFO = 'info';
    const LEVEL_WARNING = 'warning';
    const LEVEL_CRITICAL = 'critical';
    
    // Configurações padrão
    private $defaults = [
        'temp_tolerance' => 2.0,
        'temp_critical_tolerance' => 4.0,
        'esp_offline_seconds' => 300,
        'ispindel_stale_seconds' => 7200,
        'battery_warning' => 3.5,
        'battery_critical' => 3.2,
        'heap_warning' => 30000,
        'heap_critical' => 15000,
        'cooldown_minutes' => 30,
    ];
    
    public function __construct($pdo, $config = []) {
        $this->pdo = $pdo;
        $this->config = array_merge($this->defaults, $config);
    }
    
    /**
     * Executa verificação completa de todas as condições de alerta
     */
    public function checkAll($configId) {
        $alerts = [];
        
        $tempAlerts = $this->checkTemperature($configId);
        $alerts = array_merge($alerts, $tempAlerts);
        
        $espAlerts = $this->checkESPStatus($configId);
        $alerts = array_merge($alerts, $espAlerts);
        
        $ispindelAlerts = $this->checkISpindel($configId);
        $alerts = array_merge($alerts, $ispindelAlerts);
        
        $memoryAlerts = $this->checkESPMemory($configId);
        $alerts = array_merge($alerts, $memoryAlerts);
        
        foreach ($alerts as $alert) {
            $this->processAlert($alert);
        }
        
        return $alerts;
    }
    
    public function checkTemperature($configId) {
        $alerts = [];
        
        $stmt = $this->pdo->prepare("
            SELECT 
                r.temp_fermenter,
                r.temp_target,
                r.reading_timestamp,
                c.name as config_name
            FROM readings r
            JOIN configurations c ON r.config_id = c.id
            WHERE r.config_id = ?
            ORDER BY r.reading_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $reading = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if (!$reading) {
            return $alerts;
        }
        
        $tempFermenter = floatval($reading['temp_fermenter']);
        $tempTarget = floatval($reading['temp_target']);
        $diff = abs($tempFermenter - $tempTarget);
        
        if ($diff >= $this->config['temp_critical_tolerance']) {
            $direction = $tempFermenter > $tempTarget ? 'ACIMA' : 'ABAIXO';
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_TEMPERATURE,
                'level' => self::LEVEL_CRITICAL,
                'message' => "🚨 CRÍTICO: Temperatura {$direction} do alvo! " .
                            "Atual: {$tempFermenter}°C | Alvo: {$tempTarget}°C | " .
                            "Diferença: " . number_format($diff, 1) . "°C",
                'data' => [
                    'temp_current' => $tempFermenter,
                    'temp_target' => $tempTarget,
                    'difference' => $diff,
                    'config_name' => $reading['config_name']
                ]
            ];
        }
        elseif ($diff >= $this->config['temp_tolerance']) {
            $direction = $tempFermenter > $tempTarget ? 'acima' : 'abaixo';
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_TEMPERATURE,
                'level' => self::LEVEL_WARNING,
                'message' => "⚠️ Aviso: Temperatura {$direction} do alvo. " .
                            "Atual: {$tempFermenter}°C | Alvo: {$tempTarget}°C",
                'data' => [
                    'temp_current' => $tempFermenter,
                    'temp_target' => $tempTarget,
                    'difference' => $diff,
                    'config_name' => $reading['config_name']
                ]
            ];
        }
        
        return $alerts;
    }
    
    public function checkESPStatus($configId) {
        $alerts = [];
        
        $stmt = $this->pdo->prepare("
            SELECT 
                h.heartbeat_timestamp,
                h.free_heap,
                c.name as config_name,
                TIMESTAMPDIFF(SECOND, h.heartbeat_timestamp, NOW()) as seconds_ago
            FROM esp_heartbeat h
            JOIN configurations c ON h.config_id = c.id
            WHERE h.config_id = ?
            ORDER BY h.heartbeat_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $heartbeat = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if (!$heartbeat) {
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_ESP_OFFLINE,
                'level' => self::LEVEL_CRITICAL,
                'message' => "🚨 CRÍTICO: ESP nunca enviou dados! Verificar conexão WiFi.",
                'data' => ['no_heartbeat' => true]
            ];
            return $alerts;
        }
        
        $secondsAgo = intval($heartbeat['seconds_ago']);
        
        if ($secondsAgo > $this->config['esp_offline_seconds']) {
            $minutes = round($secondsAgo / 60);
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_ESP_OFFLINE,
                'level' => self::LEVEL_CRITICAL,
                'message' => "🚨 CRÍTICO: ESP OFFLINE há {$minutes} minutos! " .
                            "Controle de temperatura PARADO. Verificar imediatamente!",
                'data' => [
                    'seconds_offline' => $secondsAgo,
                    'last_seen' => $heartbeat['heartbeat_timestamp'],
                    'config_name' => $heartbeat['config_name']
                ]
            ];
        }
        
        return $alerts;
    }
    
    public function checkISpindel($configId) {
        $alerts = [];
        
        $stmt = $this->pdo->prepare("
            SELECT 
                i.temperature,
                i.gravity,
                i.battery,
                i.reading_timestamp,
                c.name as config_name,
                TIMESTAMPDIFF(SECOND, i.reading_timestamp, NOW()) as seconds_ago
            FROM ispindel_readings i
            JOIN configurations c ON i.config_id = c.id
            WHERE i.config_id = ?
            ORDER BY i.reading_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $ispindel = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if (!$ispindel) {
            return $alerts;
        }
        
        $secondsAgo = intval($ispindel['seconds_ago']);
        $battery = floatval($ispindel['battery']);
        
        if ($secondsAgo > $this->config['ispindel_stale_seconds']) {
            $hours = round($secondsAgo / 3600, 1);
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_ISPINDEL_OFFLINE,
                'level' => self::LEVEL_WARNING,
                'message' => "⚠️ iSpindel sem dados há {$hours} horas. " .
                            "Verificar bateria ou conexão WiFi.",
                'data' => [
                    'seconds_offline' => $secondsAgo,
                    'last_seen' => $ispindel['reading_timestamp'],
                    'last_battery' => $battery
                ]
            ];
        }
        
        if ($battery > 0 && $battery < $this->config['battery_critical']) {
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_BATTERY_LOW,
                'level' => self::LEVEL_CRITICAL,
                'message' => "🚨 CRÍTICO: Bateria do iSpindel MUITO BAIXA ({$battery}V). " .
                            "Recarregar urgentemente!",
                'data' => ['battery' => $battery]
            ];
        }
        elseif ($battery > 0 && $battery < $this->config['battery_warning']) {
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_BATTERY_LOW,
                'level' => self::LEVEL_WARNING,
                'message' => "⚠️ Bateria do iSpindel baixa ({$battery}V). " .
                            "Considere recarregar em breve.",
                'data' => ['battery' => $battery]
            ];
        }
        
        return $alerts;
    }
    
    public function checkESPMemory($configId) {
        $alerts = [];
        
        $stmt = $this->pdo->prepare("
            SELECT free_heap, heartbeat_timestamp
            FROM esp_heartbeat
            WHERE config_id = ?
            ORDER BY heartbeat_timestamp DESC
            LIMIT 1
        ");
        $stmt->execute([$configId]);
        $heartbeat = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if (!$heartbeat || !$heartbeat['free_heap']) {
            return $alerts;
        }
        
        $freeHeap = intval($heartbeat['free_heap']);
        
        if ($freeHeap < $this->config['heap_critical']) {
            $kb = round($freeHeap / 1024, 1);
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_MEMORY_LOW,
                'level' => self::LEVEL_CRITICAL,
                'message' => "🚨 CRÍTICO: Memória do ESP muito baixa ({$kb}KB). " .
                            "Risco de reinicialização!",
                'data' => ['free_heap' => $freeHeap]
            ];
        }
        elseif ($freeHeap < $this->config['heap_warning']) {
            $kb = round($freeHeap / 1024, 1);
            $alerts[] = [
                'config_id' => $configId,
                'type' => self::ALERT_MEMORY_LOW,
                'level' => self::LEVEL_WARNING,
                'message' => "⚠️ Memória do ESP baixa ({$kb}KB). Monitorando.",
                'data' => ['free_heap' => $freeHeap]
            ];
        }
        
        return $alerts;
    }
    
    /**
     * ✅ Cria alerta de etapa concluída SEM cooldown
     */
    public function createStageCompletedAlert($configId, $stageIndex, $stageName, $nextStageName = null) {
        $message = "✅ Etapa concluída: {$stageName}";
        if ($nextStageName) {
            $message .= " | Próxima: {$nextStageName}";
        }
        
        $alert = [
            'config_id' => $configId,
            'type' => self::ALERT_STAGE_COMPLETE,
            'level' => self::LEVEL_INFO,
            'message' => $message,
            'data' => [
                'stage_index' => $stageIndex,
                'stage_name' => $stageName,
                'next_stage' => $nextStageName
            ],
            'skip_cooldown' => true
        ];
        
        error_log("[ALERTS] createStageCompletedAlert() chamado: config_id={$configId}, stage={$stageIndex}, skip_cooldown=true");
        
        return $this->processAlert($alert);
    }
    
    public function createFermentationCompletedAlert($configId, $configName) {
        $alert = [
            'config_id' => $configId,
            'type' => self::ALERT_FERMENTATION_COMPLETE,
            'level' => self::LEVEL_INFO,
            'message' => "🎉 FERMENTAÇÃO CONCLUÍDA: {$configName}! Hora de engarrafar!",
            'data' => ['config_name' => $configName],
            'skip_cooldown' => true
        ];
        
        return $this->processAlert($alert);
    }
    
    public function createGravityReachedAlert($configId, $currentGravity, $targetGravity) {
        $alert = [
            'config_id' => $configId,
            'type' => self::ALERT_GRAVITY,
            'level' => self::LEVEL_INFO,
            'message' => "🎯 Gravidade alvo atingida! " .
                        "Atual: {$currentGravity} | Alvo: {$targetGravity}",
            'data' => [
                'current_gravity' => $currentGravity,
                'target_gravity' => $targetGravity
            ],
            'skip_cooldown' => true
        ];
        
        return $this->processAlert($alert);
    }
    
    /**
     * ✅ Processa e envia alerta - SIMPLIFICADO sem min_level
     */
    private function processAlert($alert) {
        $skipCooldown = isset($alert['skip_cooldown']) && $alert['skip_cooldown'] === true;
        
        error_log("[ALERTS] processAlert() iniciado: tipo={$alert['type']}, level={$alert['level']}, skip_cooldown=" . ($skipCooldown ? 'true' : 'false'));
        
        // Verifica cooldown apenas se não tiver flag skip_cooldown
        if (!$skipCooldown) {
            if ($this->isInCooldown($alert)) {
                error_log("[ALERTS] ❌ BLOQUEADO por cooldown: tipo={$alert['type']}, level={$alert['level']}");
                return false;
            }
            error_log("[ALERTS] ✅ Passou pelo cooldown (não há alerta recente)");
        } else {
            error_log("[ALERTS] ✅ PULOU verificação de cooldown (skip_cooldown=true)");
        }
        
        $alertId = $this->saveAlert($alert);
        
        if ($alertId) {
            error_log("[ALERTS] ✅ Alerta salvo no banco: ID={$alertId}, mensagem=\"{$alert['message']}\"");
            
            error_log("[ALERTS] Chamando sendNotification()...");
            $result = $this->sendNotification($alert);
            error_log("[ALERTS] sendNotification() retornou: " . ($result ? 'true (enviado)' : 'false (não enviado)'));
            
            return $alertId;
        } else {
            error_log("[ALERTS] ❌ ERRO: Falha ao salvar alerta no banco");
            return false;
        }
    }
    
    private function isInCooldown($alert) {
        $cooldownMinutes = $this->config['cooldown_minutes'];
        
        $stmt = $this->pdo->prepare("
            SELECT id, created_at FROM alerts 
            WHERE config_id = ? 
            AND alert_type = ? 
            AND alert_level = ?
            AND created_at > DATE_SUB(NOW(), INTERVAL ? MINUTE)
            AND is_read = 0
            LIMIT 1
        ");
        $stmt->execute([
            $alert['config_id'],
            $alert['type'],
            $alert['level'],
            $cooldownMinutes
        ]);
        
        $existingAlert = $stmt->fetch();
        
        if ($existingAlert) {
            error_log("[ALERTS] Cooldown ativo: encontrado alerta ID={$existingAlert['id']} criado em {$existingAlert['created_at']}");
            return true;
        }
        
        return false;
    }
    
    private function saveAlert($alert) {
        try {
            $stmt = $this->pdo->prepare("
                INSERT INTO alerts (config_id, alert_type, alert_level, message, is_read, created_at)
                VALUES (?, ?, ?, ?, 0, NOW())
            ");
            
            $stmt->execute([
                $alert['config_id'],
                $alert['type'],
                $alert['level'],
                $alert['message']
            ]);
            
            return $this->pdo->lastInsertId();
        } catch (Exception $e) {
            error_log("[ALERTS] ❌ Erro ao salvar alerta: " . $e->getMessage());
            return false;
        }
    }
    
    /**
     * ✅ SIMPLIFICADO: Envia notificação SEMPRE (sem filtro de min_level)
     * Apenas verifica se o sistema está ativado e se há canais configurados
     */
    private function sendNotification($alert) {
        $notifyConfig = $this->getNotificationConfig();
        
        if (!$notifyConfig || !$notifyConfig['enabled']) {
            error_log("[ALERTS] ❌ Sistema de alertas está DESATIVADO (enabled=0)");
            return false;
        }
        
        error_log("[ALERTS] ✅ Sistema ativado, enviando alerta: tipo={$alert['type']}, level={$alert['level']}");
        
        $sent = false;
        
        // WhatsApp
        if (!empty($notifyConfig['whatsapp_phone']) && !empty($notifyConfig['whatsapp_apikey'])) {
            error_log("[ALERTS] Tentando enviar via WhatsApp...");
            $sent = $this->sendWhatsApp(
                $notifyConfig['whatsapp_phone'],
                $notifyConfig['whatsapp_apikey'],
                $alert['message']
            );
        } else {
            error_log("[ALERTS] WhatsApp não configurado (phone ou apikey vazio)");
        }
        
        // Telegram
        if (!empty($notifyConfig['telegram_chat_id']) && !empty($notifyConfig['telegram_bot_token'])) {
            error_log("[ALERTS] Tentando enviar via Telegram...");
            $telegramSent = $this->sendTelegram(
                $notifyConfig['telegram_bot_token'],
                $notifyConfig['telegram_chat_id'],
                $alert['message']
            );
            if (!$telegramSent) {
                error_log("[ALERTS] ❌ Falha no envio Telegram!");
            }
            $sent = $telegramSent || $sent;
        } else {
            error_log("[ALERTS] Telegram não configurado (token ou chat_id vazio)");
        }
        
        if (!$sent) {
            error_log("[ALERTS] ⚠️ NENHUM canal de notificação enviou com sucesso!");
        }
        
        return $sent;
    }
    
    private function getNotificationConfig() {
        $stmt = $this->pdo->query("
            SELECT config_key, config_value 
            FROM system_config 
            WHERE config_key LIKE 'alert_%'
        ");
        
        $config = [];
        while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
            $key = str_replace('alert_', '', $row['config_key']);
            $config[$key] = $row['config_value'];
        }
        
        return [
            'enabled' => ($config['enabled'] ?? '0') === '1',
            'whatsapp_phone' => $config['whatsapp_phone'] ?? '',
            'whatsapp_apikey' => $config['whatsapp_apikey'] ?? '',
            'telegram_bot_token' => $config['telegram_bot_token'] ?? '',
            'telegram_chat_id' => $config['telegram_chat_id'] ?? ''
        ];
    }
    
    private function sendWhatsApp($phone, $apikey, $message) {
        $url = "https://api.callmebot.com/whatsapp.php?" . http_build_query([
            'phone' => $phone,
            'text' => $message,
            'apikey' => $apikey
        ]);
        
        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL => $url,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 10,
            CURLOPT_SSL_VERIFYPEER => true
        ]);
        
        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $error = curl_error($ch);
        curl_close($ch);
        
        error_log("[ALERTS] WhatsApp: HTTP {$httpCode} - " . substr($response, 0, 100));
        if ($error) {
            error_log("[ALERTS] WhatsApp curl error: {$error}");
        }
        
        return $httpCode === 200 && strpos($response, 'Message queued') !== false;
    }
    
    /**
     * ✅ CORREÇÃO: Removido parse_mode => 'HTML' que causava falha com emojis
     */
    private function sendTelegram($botToken, $chatId, $message) {
        $url = "https://api.telegram.org/bot{$botToken}/sendMessage";
        
        $postData = [
            'chat_id' => $chatId,
            'text' => $message
        ];
        
        error_log("[ALERTS] Telegram request: chat_id={$chatId}, msg_length=" . strlen($message));
        
        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL => $url,
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => http_build_query($postData),
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 10
        ]);
        
        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $error = curl_error($ch);
        curl_close($ch);
        
        error_log("[ALERTS] Telegram: HTTP {$httpCode} - " . substr($response, 0, 200));
        if ($error) {
            error_log("[ALERTS] Telegram curl error: {$error}");
        }
        
        $result = json_decode($response, true);
        
        if ($httpCode !== 200 || !($result['ok'] ?? false)) {
            $desc = $result['description'] ?? 'Unknown error';
            error_log("[ALERTS] ❌ Telegram FALHOU: {$desc}");
            return false;
        }
        
        error_log("[ALERTS] ✅ Telegram enviado com sucesso! message_id=" . ($result['result']['message_id'] ?? '?'));
        return true;
    }
    
    public function getUnreadAlerts($configId = null) {
        $sql = "SELECT * FROM alerts WHERE is_read = 0";
        $params = [];
        
        if ($configId) {
            $sql .= " AND config_id = ?";
            $params[] = $configId;
        }
        
        $sql .= " ORDER BY created_at DESC LIMIT 50";
        
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($params);
        
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    }
    
    public function markAsRead($alertId) {
        $stmt = $this->pdo->prepare("UPDATE alerts SET is_read = 1 WHERE id = ?");
        return $stmt->execute([$alertId]);
    }
    
    public function markAllAsRead($configId = null) {
        $sql = "UPDATE alerts SET is_read = 1 WHERE is_read = 0";
        $params = [];
        
        if ($configId) {
            $sql .= " AND config_id = ?";
            $params[] = $configId;
        }
        
        $stmt = $this->pdo->prepare($sql);
        return $stmt->execute($params);
    }
    
    public function resolveAlert($alertId) {
        $stmt = $this->pdo->prepare("UPDATE alerts SET is_read = 1, resolved_at = NOW() WHERE id = ?");
        return $stmt->execute([$alertId]);
    }
}