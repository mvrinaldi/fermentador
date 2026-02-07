<?php
/**
 * Ponte entre api.php e AlertSystem
 * 
 * @author Marcos Rinaldi
 * @version 1.3 - Logs melhorados para debug
 * @date Fevereiro 2026
 */

// Caminho flexível que funciona em QUALQUER estrutura
$alertSystemPath = __DIR__ . '/../classes/AlertSystem.php';
if (!file_exists($alertSystemPath)) {
    $alertSystemPath = __DIR__ . '/../AlertSystem.php';
}
if (!file_exists($alertSystemPath)) {
    $alertSystemPath = dirname(dirname(__DIR__)) . '/classes/AlertSystem.php';
}
if (!file_exists($alertSystemPath)) {
    error_log("[ALERT ERROR] AlertSystem.php não encontrado. Procurado em:");
    error_log("  1. " . __DIR__ . '/../classes/AlertSystem.php');
    error_log("  2. " . __DIR__ . '/../AlertSystem.php');
    error_log("  3. " . dirname(dirname(__DIR__)) . '/classes/AlertSystem.php');
    throw new Exception('AlertSystem.php not found');
}

require_once $alertSystemPath;
error_log("[ALERTS] AlertSystem carregado de: $alertSystemPath");

class AlertIntegration {
    
    private static $alertSystem = null;
    private static $lastCheck = 0;
    private static $checkInterval = 60;
    
    public static function init($pdo) {
        if (self::$alertSystem === null) {
            self::$alertSystem = new AlertSystem($pdo);
            error_log("[ALERTS] AlertSystem inicializado com sucesso");
        }
        return self::$alertSystem;
    }
    
    /**
     * Verifica alertas após heartbeat do ESP
     */
    public static function checkAlertsOnHeartbeat($pdo, $configId) {
        $now = time();
        
        if ($now - self::$lastCheck < self::$checkInterval) {
            return [];
        }
        
        self::$lastCheck = $now;
        
        try {
            $alertSystem = self::init($pdo);
            $alerts = $alertSystem->checkAll($configId);
            
            if (count($alerts) > 0) {
                error_log("[ALERTS] Heartbeat gerou " . count($alerts) . " alertas");
            }
            
            return $alerts;
            
        } catch (Exception $e) {
            error_log("[ALERTS] ❌ Erro em checkAlertsOnHeartbeat: " . $e->getMessage());
            error_log("[ALERTS] Stack trace: " . $e->getTraceAsString());
            return [];
        }
    }
    
    /**
     * ✅ Dispara alerta quando etapa é concluída
     */
    public static function onStageCompleted($pdo, $configId, $completedStageIndex, $completedStageName, $nextStageName = null) {
        try {
            error_log("[ALERTS] ========================================");
            error_log("[ALERTS] onStageCompleted() CHAMADO!");
            error_log("[ALERTS] config_id={$configId}, stage={$completedStageIndex}, nome='{$completedStageName}', next='" . ($nextStageName ?? 'null') . "'");
            error_log("[ALERTS] ========================================");
            
            $alertSystem = self::init($pdo);
            
            if (!$alertSystem) {
                error_log("[ALERTS] ❌ ERRO CRÍTICO: AlertSystem não foi inicializado!");
                return null;
            }
            
            $alert = $alertSystem->createStageCompletedAlert(
                $configId, 
                $completedStageIndex, 
                $completedStageName,
                $nextStageName
            );
            
            if ($alert === false || $alert === null) {
                error_log("[ALERTS] ⚠️ createStageCompletedAlert() retornou NULL/FALSE");
            } else {
                error_log("[ALERTS] ✅ Alerta de etapa criado com sucesso! ID={$alert}");
            }
            
            return $alert;
            
        } catch (Exception $e) {
            error_log("[ALERTS] ❌ EXCEÇÃO em onStageCompleted: " . $e->getMessage());
            error_log("[ALERTS] Arquivo: " . $e->getFile() . " Linha: " . $e->getLine());
            error_log("[ALERTS] Stack trace: " . $e->getTraceAsString());
            return null;
        }
    }
    
    /**
     * ✅ Dispara alerta quando fermentação é concluída
     */
    public static function onFermentationCompleted($pdo, $configId, $configName) {
        try {
            error_log("[ALERTS] ========================================");
            error_log("[ALERTS] onFermentationCompleted() CHAMADO!");
            error_log("[ALERTS] config_id={$configId}, nome='{$configName}'");
            error_log("[ALERTS] ========================================");
            
            $alertSystem = self::init($pdo);
            
            if (!$alertSystem) {
                error_log("[ALERTS] ❌ ERRO: AlertSystem não foi inicializado!");
                return null;
            }
            
            $alert = $alertSystem->createFermentationCompletedAlert($configId, $configName);
            
            if ($alert) {
                error_log("[ALERTS] ✅ Alerta de fermentação concluída criado: ID={$alert}");
            }
            
            return $alert;
            
        } catch (Exception $e) {
            error_log("[ALERTS] ❌ EXCEÇÃO em onFermentationCompleted: " . $e->getMessage());
            error_log("[ALERTS] Stack trace: " . $e->getTraceAsString());
            return null;
        }
    }
    
    /**
     * ✅ Dispara alerta quando gravidade é atingida
     */
    public static function onGravityReached($pdo, $configId, $currentGravity, $targetGravity) {
        try {
            error_log("[ALERTS] ========================================");
            error_log("[ALERTS] onGravityReached() CHAMADO!");
            error_log("[ALERTS] config_id={$configId}, atual={$currentGravity}, alvo={$targetGravity}");
            error_log("[ALERTS] ========================================");
            
            $alertSystem = self::init($pdo);
            
            if (!$alertSystem) {
                error_log("[ALERTS] ❌ ERRO: AlertSystem não foi inicializado!");
                return null;
            }
            
            $alert = $alertSystem->createGravityReachedAlert($configId, $currentGravity, $targetGravity);
            
            if ($alert) {
                error_log("[ALERTS] ✅ Alerta de gravidade atingida criado: ID={$alert}");
            }
            
            return $alert;
            
        } catch (Exception $e) {
            error_log("[ALERTS] ❌ EXCEÇÃO em onGravityReached: " . $e->getMessage());
            error_log("[ALERTS] Stack trace: " . $e->getTraceAsString());
            return null;
        }
    }
}