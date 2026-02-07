<?php
// classes/DatabaseCleanup.php

class DatabaseCleanup {
    
    /**
     * Configuração de limites por tabela
     */
    private static $tableConfig = [
        'readings' => ['keep' => 200, 'timestamp' => 'reading_timestamp'],
        'controller_states' => ['keep' => 200, 'timestamp' => 'state_timestamp'],
        'esp_heartbeat' => ['keep' => 50, 'timestamp' => 'heartbeat_timestamp'],
        'fermentation_states' => ['keep' => 100, 'timestamp' => 'state_timestamp'],
        'ispindel_readings' => ['keep' => 500, 'timestamp' => 'reading_timestamp']
    ];
    
    /**
     * Limpa registros antigos de uma tabela específica
     */
    public static function cleanupTable($pdo, $tableName, $configId, $customKeep = null) {
        try {
            // Validar tabela
            if (!isset(self::$tableConfig[$tableName])) {
                error_log("[CLEANUP ERROR] Tabela inválida: {$tableName}");
                return false;
            }
            
            $config = self::$tableConfig[$tableName];
            $keepCount = $customKeep ?? $config['keep'];
            $timestampColumn = $config['timestamp'];
            
            // 1. Contar registros do config_id
            $stmt = $pdo->prepare("SELECT COUNT(*) as total FROM `{$tableName}` WHERE config_id = ?");
            $stmt->execute([$configId]);
            $count = $stmt->fetch(PDO::FETCH_ASSOC)['total'];
            
            // 2. Se exceder limite, deletar antigos
            if ($count > $keepCount) {
                // Buscar timestamp de corte
                $stmt = $pdo->prepare("
                    SELECT `{$timestampColumn}` as cutoff_time 
                    FROM `{$tableName}` 
                    WHERE config_id = ? 
                    ORDER BY `{$timestampColumn}` DESC 
                    LIMIT 1 OFFSET ?
                ");
                $stmt->execute([$configId, $keepCount]);
                $result = $stmt->fetch(PDO::FETCH_ASSOC);
                
                if ($result && !empty($result['cutoff_time'])) {
                    // Deletar registros antigos
                    $stmt = $pdo->prepare("
                        DELETE FROM `{$tableName}` 
                        WHERE config_id = ? 
                        AND `{$timestampColumn}` < ?
                    ");
                    $stmt->execute([$configId, $result['cutoff_time']]);
                    
                    $deleted = $stmt->rowCount();
                    if ($deleted > 0) {
                        error_log("[CLEANUP] {$tableName}: config_id={$configId}, removidos {$deleted} registros");
                    }
                    return $deleted;
                }
            }
            
            return 0;
            
        } catch (Exception $e) {
            error_log("[CLEANUP ERROR] {$tableName}: " . $e->getMessage());
            return false;
        }
    }
    
    /**
     * Limpa órfãos de todas as tabelas (sem config_id)
     */
    public static function cleanupOrphans($pdo) {
        $cleaned = 0;
        
        foreach (self::$tableConfig as $tableName => $config) {
            try {
                $stmt = $pdo->prepare("DELETE FROM `{$tableName}` WHERE config_id IS NULL LIMIT 500");
                $stmt->execute();
                $deleted = $stmt->rowCount();
                
                if ($deleted > 0) {
                    error_log("[CLEANUP ORPHANS] {$tableName}: removidos {$deleted} órfãos");
                    $cleaned += $deleted;
                }
            } catch (Exception $e) {
                error_log("[CLEANUP ORPHANS ERROR] {$tableName}: " . $e->getMessage());
            }
        }
        
        return $cleaned;
    }
    
    /**
     * Limpeza agressiva de uma tabela (usado em emergências)
     */
    public static function aggressiveCleanup($pdo, $tableName, $configId, $keepCount) {
        try {
            if (!isset(self::$tableConfig[$tableName])) {
                return false;
            }
            
            $timestampColumn = self::$tableConfig[$tableName]['timestamp'];
            
            // Deletar TUDO exceto os últimos N registros
            $stmt = $pdo->prepare("
                DELETE FROM `{$tableName}` 
                WHERE config_id = ? 
                AND `{$timestampColumn}` < (
                    SELECT cutoff FROM (
                        SELECT `{$timestampColumn}` as cutoff 
                        FROM `{$tableName}` 
                        WHERE config_id = ? 
                        ORDER BY `{$timestampColumn}` DESC 
                        LIMIT 1 OFFSET ?
                    ) as subquery
                )
            ");
            
            $stmt->execute([$configId, $configId, $keepCount]);
            $deleted = $stmt->rowCount();
            
            if ($deleted > 0) {
                error_log("[AGGRESSIVE CLEANUP] {$tableName}: config_id={$configId}, removidos {$deleted}");
            }
            
            return $deleted;
            
        } catch (Exception $e) {
            error_log("[AGGRESSIVE CLEANUP ERROR] {$tableName}: " . $e->getMessage());
            return false;
        }
    }
}