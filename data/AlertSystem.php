<?php
/**
 * Sistema de Alertas para Controle de Fermentação
 * 
 * Monitora condições críticas e envia notificações via WhatsApp/Telegram
 * 
 * @author Marcos Rinaldi
 * @version 1.0
 * @date Janeiro 2026
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
    
    // Níveis de alerta
    const LEVEL_INFO = 'info';
    const LEVEL_WARNING = 'warning';
    const LEVEL_CRITICAL = 'critical';
    
    // Configurações padrão
    private $defaults = [
        'temp_tolerance' => 2.0,           // °C de tolerância antes de alertar
        'temp_critical_tolerance' => 4.0,  // °C para alerta crítico
        'esp_offline_seconds' => 300,      // 5 minutos sem heartbeat
        'ispindel_stale_seconds' => 7200,  // 2 horas sem dados
        'battery_warning' => 3.5,          // Volts
        'battery_critical' => 3.2,         // Volts
        'heap_warning' => 30000,           // Bytes
        'heap_critical' => 15000,          // Bytes
        'cooldown_minutes' => 30,          // Não repetir alerta em X minutos
    ];
    
    public function __construct($pdo, $config = []) {
        $this->pdo = $pdo;
        $this->config = array_merge($this->defaults, $config);
    }
    
    /**
     * Executa verificação completa de todas as condições de alerta
     * 
     * @param int $configId ID da fermentação ativa
     * @return array Lista de alertas gerados
     */
    public function checkAll($configId) {
        $alerts = [];
        
        // 1. Verificar temperatura
        $tempAlerts = $this->checkTemperature($configId);
        $alerts = array_merge($alerts, $tempAlerts);
        
        // 2. Verificar ESP online
        $espAlerts = $this->checkESPStatus($configId);
        $alerts = array_merge($alerts, $espAlerts);
        
        // 3. Verificar iSpindel
        $ispindelAlerts = $this->checkISpindel($configId);
        $alerts = array_merge($alerts, $ispindelAlerts);
        
        // 4. Verificar memória do ESP
        $memoryAlerts = $this->checkESPMemory($configId);
        $alerts = array_merge($alerts, $memoryAlerts);
        
        // Processar e salvar alertas
        foreach ($alerts as $alert) {
            $this->processAlert($alert);
        }
        
        return $alerts;
    }
    
    /**
     * Verifica se temperatura está fora do range aceitável
     */
    public function checkTemperature($configId) {
        $alerts = [];
        
        // Buscar última leitura e temperatura alvo
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
        
        // Verificar tolerância crítica
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
        // Verificar tolerância de aviso
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
    
    /**
     * Verifica se ESP está online (recebendo heartbeats)
     */
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
            // Nenhum heartbeat encontrado
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
    
    /**
     * Verifica status do iSpindel
     */
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
            return $alerts; // Sem iSpindel configurado, ok
        }
        
        $secondsAgo = intval($ispindel['seconds_ago']);
        $battery = floatval($ispindel['battery']);
        
        // Verificar se dados estão obsoletos
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
        
        // Verificar bateria crítica
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
        // Verificar bateria baixa
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
    
    /**
     * Verifica memória livre do ESP
     */
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
     * Cria alerta de etapa concluída
     */
    public function createStageCompletedAlert($configId, $stageIndex, $stageName, $nextStageName = null) {
        $message = "✅ Etapa {$stageIndex} concluída: {$stageName}";
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
            ]
        ];
        
        $this->processAlert($alert);
        return $alert;
    }
    
    /**
     * Cria alerta de fermentação concluída
     */
    public function createFermentationCompletedAlert($configId, $configName) {
        $alert = [
            'config_id' => $configId,
            'type' => self::ALERT_FERMENTATION_COMPLETE,
            'level' => self::LEVEL_INFO,
            'message' => "🎉 FERMENTAÇÃO CONCLUÍDA: {$configName}! Hora de engarrafar!",
            'data' => ['config_name' => $configName]
        ];
        
        $this->processAlert($alert);
        return $alert;
    }
    
    /**
     * Cria alerta de gravidade alvo atingida
     */
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
            ]
        ];
        
        $this->processAlert($alert);
        return $alert;
    }
    
    /**
     * Processa um alerta: verifica cooldown, salva no banco, envia notificação
     */
    private function processAlert($alert) {
        // Verificar cooldown (não repetir alerta recente)
        if ($this->isInCooldown($alert)) {
            return false;
        }
        
        // Salvar no banco
        $alertId = $this->saveAlert($alert);
        
        // Enviar notificação
        if ($alertId) {
            $this->sendNotification($alert);
        }
        
        return $alertId;
    }
    
    /**
     * Verifica se alerta similar foi enviado recentemente
     */
    private function isInCooldown($alert) {
        $cooldownMinutes = $this->config['cooldown_minutes'];
        
        $stmt = $this->pdo->prepare("
            SELECT id FROM alerts 
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
        
        return $stmt->fetch() !== false;
    }
    
    /**
     * Salva alerta no banco de dados
     */
    private function saveAlert($alert) {
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
    }
    
    /**
     * Envia notificação (WhatsApp/Telegram)
     */
    private function sendNotification($alert) {
        // Buscar configurações de notificação
        $notifyConfig = $this->getNotificationConfig();
        
        if (!$notifyConfig || !$notifyConfig['enabled']) {
            return false;
        }
        
        // Filtrar por nível (só enviar se nível atende mínimo configurado)
        if (!$this->shouldNotify($alert['level'], $notifyConfig['min_level'])) {
            return false;
        }
        
        $sent = false;
        
        // WhatsApp via CallMeBot
        if (!empty($notifyConfig['whatsapp_phone']) && !empty($notifyConfig['whatsapp_apikey'])) {
            $sent = $this->sendWhatsApp(
                $notifyConfig['whatsapp_phone'],
                $notifyConfig['whatsapp_apikey'],
                $alert['message']
            );
        }
        
        // Telegram
        if (!empty($notifyConfig['telegram_chat_id']) && !empty($notifyConfig['telegram_bot_token'])) {
            $sent = $this->sendTelegram(
                $notifyConfig['telegram_bot_token'],
                $notifyConfig['telegram_chat_id'],
                $alert['message']
            ) || $sent;
        }
        
        return $sent;
    }
    
    /**
     * Verifica se deve notificar baseado no nível
     */
    private function shouldNotify($alertLevel, $minLevel) {
        $levels = [
            self::LEVEL_INFO => 1,
            self::LEVEL_WARNING => 2,
            self::LEVEL_CRITICAL => 3
        ];
        
        return $levels[$alertLevel] >= $levels[$minLevel];
    }
    
    /**
     * Busca configurações de notificação do banco
     */
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
            'min_level' => $config['min_level'] ?? self::LEVEL_WARNING,
            'whatsapp_phone' => $config['whatsapp_phone'] ?? '',
            'whatsapp_apikey' => $config['whatsapp_apikey'] ?? '',
            'telegram_bot_token' => $config['telegram_bot_token'] ?? '',
            'telegram_chat_id' => $config['telegram_chat_id'] ?? ''
        ];
    }
    
    /**
     * Envia mensagem via WhatsApp (CallMeBot)
     */
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
        curl_close($ch);
        
        // Log do envio
        error_log("WhatsApp Alert: HTTP {$httpCode} - " . substr($response, 0, 100));
        
        return $httpCode === 200 && strpos($response, 'Message queued') !== false;
    }
    
    /**
     * Envia mensagem via Telegram
     */
    private function sendTelegram($botToken, $chatId, $message) {
        $url = "https://api.telegram.org/bot{$botToken}/sendMessage";
        
        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL => $url,
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => http_build_query([
                'chat_id' => $chatId,
                'text' => $message,
                'parse_mode' => 'HTML'
            ]),
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 10
        ]);
        
        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
        
        // Log do envio
        error_log("Telegram Alert: HTTP {$httpCode} - " . substr($response, 0, 100));
        
        $result = json_decode($response, true);
        return $httpCode === 200 && ($result['ok'] ?? false);
    }
    
    /**
     * Retorna alertas não lidos
     */
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
    
    /**
     * Marca alerta como lido
     */
    public function markAsRead($alertId) {
        $stmt = $this->pdo->prepare("
            UPDATE alerts SET is_read = 1 WHERE id = ?
        ");
        return $stmt->execute([$alertId]);
    }
    
    /**
     * Marca todos alertas como lidos
     */
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
    
    /**
     * Resolve alerta (marca como resolvido)
     */
    public function resolveAlert($alertId) {
        $stmt = $this->pdo->prepare("
            UPDATE alerts SET is_read = 1, resolved_at = NOW() WHERE id = ?
        ");
        return $stmt->execute([$alertId]);
    }
}