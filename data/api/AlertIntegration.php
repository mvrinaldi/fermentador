<?php
/**
 * @author Marcos Rinaldi
 * @version 1.0
 */

require_once __DIR__ . '/../AlertSystem.php';

class AlertIntegration {
    
    private static $alertSystem = null;
    private static $lastCheck = 0;
    private static $checkInterval = 60; // Verificar a cada 60 segundos
    
    /**
     * Inicializa o sistema de alertas
     */
    public static function init($pdo) {
        if (self::$alertSystem === null) {
            self::$alertSystem = new AlertSystem($pdo);
        }
        return self::$alertSystem;
    }
    
    /**
     * Verifica alertas se passou tempo suficiente desde última verificação
     * Chamar após receber heartbeat do ESP
     */
    public static function checkAlertsOnHeartbeat($pdo, $configId) {
        $now = time();
        
        // Evitar verificações muito frequentes
        if ($now - self::$lastCheck < self::$checkInterval) {
            return [];
        }
        
        self::$lastCheck = $now;
        
        $alertSystem = self::init($pdo);
        return $alertSystem->checkAll($configId);
    }
    
    /**
     * Cria alerta quando etapa é concluída
     * Chamar no endpoint /stage quando etapa avança
     */
    public static function onStageCompleted($pdo, $configId, $completedStageIndex, $completedStageName, $nextStageName = null) {
        $alertSystem = self::init($pdo);
        return $alertSystem->createStageCompletedAlert(
            $configId, 
            $completedStageIndex, 
            $completedStageName,
            $nextStageName
        );
    }
    
    /**
     * Cria alerta quando fermentação é concluída
     * Chamar quando última etapa termina
     */
    public static function onFermentationCompleted($pdo, $configId, $configName) {
        $alertSystem = self::init($pdo);
        return $alertSystem->createFermentationCompletedAlert($configId, $configName);
    }
    
    /**
     * Cria alerta quando gravidade alvo é atingida
     * Chamar no endpoint /target ou quando detectar gravidade atingida
     */
    public static function onGravityReached($pdo, $configId, $currentGravity, $targetGravity) {
        $alertSystem = self::init($pdo);
        return $alertSystem->createGravityReachedAlert($configId, $currentGravity, $targetGravity);
    }
}
