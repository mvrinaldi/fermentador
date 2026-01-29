# Dicionário de Dados - Banco de Dados MySQL

**Banco:** `u946332176_bd_geral`  
**Servidor:** MariaDB 11.8.3  
**Charset:** `utf8mb4`  
**Collation:** `utf8mb4_unicode_ci`

---

## Diagrama Entidade-Relacionamento (ERD)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NÚCLEO DO SISTEMA                              │
└─────────────────────────────────────────────────────────────────────────────┘

  ┌────────────────┐
  │     users      │
  │────────────────│
  │ PK id          │──────────────────┐
  │ email (UNIQUE) │                  │
  │ password_hash  │                  │
  │ is_active      │                  │
  │ last_login     │                  │
  │ created_at     │                  │
  └────────────────┘                  │
                                      │ 1:N
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │                          configurations                                 │
  │────────────────────────────────────────────────────────────────────────│
  │ PK id                    │ FK user_id                                  │
  │ name                     │ status (pending/active/paused/completed)    │
  │ current_stage_index      │ current_target_temp                         │
  │ started_at / paused_at / completed_at / times_used                     │
  └────────────────────────────────────────────────────────────────────────┘
           │
           │ 1:N (CASCADE DELETE)
           ├──────────────────────┬──────────────────────┬─────────────────────┐
           │                      │                      │                     │
           ▼                      ▼                      ▼                     ▼
  ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
  │     stages      │   │    readings     │   │ controller_     │   │ fermentation_   │
  │─────────────────│   │─────────────────│   │ states          │   │ states          │
  │ PK id           │   │ PK id           │   │─────────────────│   │─────────────────│
  │ FK config_id    │   │ FK config_id    │   │ PK id           │   │ PK id           │
  │ stage_index     │   │ temp_fridge     │   │ FK config_id    │   │ FK config_id    │
  │ type            │   │ temp_fermenter  │   │ setpoint        │   │ state_data(JSON)│
  │ target_temp     │   │ temp_target     │   │ cooling         │   │ state_timestamp │
  │ duration        │   │ reading_        │   │ heating         │   └─────────────────┘
  │ target_gravity  │   │ timestamp       │   │ state_timestamp │
  │ max_duration    │   │ reading_date    │   └─────────────────┘
  │ start_temp      │   │ (GENERATED)     │
  │ ramp_time       │   └─────────────────┘
  │ actual_rate     │
  │ direction       │
  │ status          │
  └─────────────────┘

           │
           │ 1:N (SET NULL on delete)
           ├──────────────────────┬──────────────────────┐
           │                      │                      │
           ▼                      ▼                      ▼
  ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
  │ esp_heartbeat   │   │ ispindel_       │   │ fermentation_   │
  │─────────────────│   │ readings        │   │ events          │
  │ PK id           │   │─────────────────│   │─────────────────│
  │ FK config_id    │   │ PK id           │   │ PK id           │
  │ uptime_seconds  │   │ FK config_id    │   │ FK config_id    │
  │ free_heap       │   │ name            │   │ event_type      │
  │ temp_fermenter  │   │ temperature     │   │ event_data(JSON)│
  │ temp_fridge     │   │ gravity         │   │ event_timestamp │
  │ cooler_active   │   │ battery         │   └─────────────────┘
  │ heater_active   │   │ reading_        │
  │ control_status  │   │ timestamp       │
  │ heartbeat_      │   └─────────────────┘
  │ timestamp       │
  └─────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                         TABELAS DE SUPORTE                                  │
└─────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
  │  system_config  │   │    devices      │   │ detected_       │
  │─────────────────│   │─────────────────│   │ sensors         │
  │ PK id           │   │ PK id           │   │─────────────────│
  │ config_key      │   │ device_id       │   │ PK id           │
  │ (UNIQUE)        │   │ (UNIQUE)        │   │ address(UNIQUE) │
  │ config_value    │   │ device_type     │   │ detected_at     │
  │ updated_at      │   │ sensor_data     │   │ last_seen       │
  └─────────────────┘   │ last_seen       │   └─────────────────┘
                        │ is_online       │
                        └─────────────────┘

  ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
  │  esp_commands   │   │ action_history  │   │    alerts       │
  │─────────────────│   │─────────────────│   │─────────────────│
  │ PK id           │   │ PK id           │   │ PK id           │
  │ command         │   │ FK user_id      │   │ FK config_id    │
  │ status          │   │ FK config_id    │   │ alert_type      │
  │ created_at      │   │ action_type     │   │ alert_level     │
  │ executed_at     │   │ action_details  │   │ message         │
  └─────────────────┘   │ action_timestamp│   │ is_read         │
                        └─────────────────┘   │ created_at      │
                                              │ resolved_at     │
  ┌─────────────────┐                         └─────────────────┘
  │  sensor_cache   │
  │─────────────────│
  │ PK id           │
  │ sensor_type     │
  │ (UNIQUE)        │
  │ temperature     │
  │ updated_at      │
  └─────────────────┘
```

---

## Tabelas Detalhadas

### 1. `users` - Usuários do Sistema

Armazena credenciais de autenticação dos usuários.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `email` | VARCHAR(255) | NÃO | UNIQUE, INDEX | - | Email de login |
| `password_hash` | VARCHAR(255) | NÃO | - | - | Hash bcrypt da senha |
| `created_at` | TIMESTAMP | SIM | - | CURRENT_TIMESTAMP | Data de criação |
| `last_login` | TIMESTAMP | SIM | - | NULL | Último login |
| `is_active` | TINYINT(1) | SIM | INDEX | 1 | Ativo (1) ou bloqueado (0) |

**Índices:**
- `PRIMARY KEY (id)`
- `UNIQUE KEY email (email)`
- `KEY idx_email (email)`
- `KEY idx_active (is_active)`

---

### 2. `configurations` - Configurações de Fermentação

Receitas/configurações de fermentação criadas pelos usuários.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `user_id` | INT(11) | NÃO | FK, INDEX | - | Proprietário |
| `name` | VARCHAR(100) | NÃO | - | - | Nome (ex: "IPA 2026") |
| `status` | ENUM | SIM | INDEX | 'pending' | Estado atual |
| `created_at` | TIMESTAMP | SIM | - | CURRENT_TIMESTAMP | Criação |
| `started_at` | TIMESTAMP | SIM | - | NULL | Início |
| `paused_at` | TIMESTAMP | SIM | - | NULL | Pausa |
| `completed_at` | TIMESTAMP | SIM | - | NULL | Conclusão |
| `updated_at` | TIMESTAMP | SIM | - | ON UPDATE | Atualização |
| `times_used` | INT(11) | SIM | - | 0 | Vezes usada |
| `current_stage_index` | INT(11) | SIM | - | 0 | Etapa atual (0-based) |
| `current_target_temp` | DECIMAL(4,1) | SIM | - | NULL | Temp alvo atual |

**Valores de `status`:**
| Valor | Descrição |
|-------|-----------|
| `pending` | Criada, nunca iniciada |
| `active` | Em andamento |
| `paused` | Pausada |
| `completed` | Concluída |

**Índices:**
- `PRIMARY KEY (id)`
- `KEY idx_status (status)`
- `KEY idx_user (user_id)`
- `KEY idx_active (status, user_id)`

**Foreign Keys:**
- `FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE`

---

### 3. `stages` - Etapas de Fermentação

Define as etapas sequenciais de cada configuração.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | NÃO | FK, UNIQUE | - | Configuração pai |
| `stage_index` | INT(11) | NÃO | UNIQUE | - | Ordem (0, 1, 2...) |
| `type` | ENUM | NÃO | - | - | Tipo da etapa |
| `target_temp` | DECIMAL(5,2) | SIM | - | NULL | Temp alvo (°C) |
| `duration` | DECIMAL(5,2) | SIM | - | NULL | Duração (dias) |
| `target_gravity` | DECIMAL(5,3) | SIM | - | NULL | Gravidade alvo |
| `max_duration` | DECIMAL(5,2) | SIM | - | NULL | Duração máxima |
| `start_temp` | DECIMAL(5,2) | SIM | - | NULL | Temp inicial (rampa) |
| `ramp_time` | INT(11) | SIM | - | NULL | Tempo rampa (horas) |
| `max_ramp_rate` | DECIMAL(4,2) | SIM | - | NULL | Taxa máxima (°C/dia) |
| `actual_rate` | DECIMAL(4,2) | SIM | - | NULL | Taxa real (°C/dia) |
| `direction` | ENUM('up','down') | SIM | - | NULL | Direção da rampa |
| `status` | ENUM | SIM | INDEX | 'pending' | Status da etapa |
| `start_time` | TIMESTAMP | SIM | - | NULL | Início |
| `end_time` | TIMESTAMP | SIM | - | NULL | Fim |
| `target_reached_time` | TIMESTAMP | SIM | - | NULL | Alvo atingido |

**Tipos de Etapa (`type`):**
| Tipo | Descrição | Campos Utilizados |
|------|-----------|-------------------|
| `temperature` | Mantém temperatura por tempo | target_temp, duration |
| `ramp` | Transição gradual | start_temp, target_temp, ramp_time, direction |
| `gravity` | Aguarda gravidade alvo | target_temp, target_gravity |
| `gravity_time` | Gravidade com timeout | target_temp, target_gravity, max_duration |

**Status da Etapa (`status`):**
| Valor | Descrição |
|-------|-----------|
| `pending` | Aguardando início |
| `running` | Em execução |
| `waiting` | Aguardando condição |
| `completed` | Concluída |
| `cancelled` | Cancelada |

**Índices:**
- `PRIMARY KEY (id)`
- `UNIQUE KEY unique_config_stage (config_id, stage_index)`
- `KEY idx_config_stage (config_id, stage_index)`
- `KEY idx_status (status)`

**Foreign Keys:**
- `FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE CASCADE`

---

### 4. `readings` - Leituras de Temperatura

Histórico de leituras dos sensores DS18B20.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | SIM | FK, INDEX | NULL | Fermentação (NULL se inativa) |
| `temp_fridge` | DECIMAL(4,1) | NÃO | - | - | Temp geladeira (°C) |
| `temp_fermenter` | DECIMAL(4,1) | NÃO | - | - | Temp fermentador (°C) |
| `temp_target` | DECIMAL(4,1) | NÃO | - | - | Temp alvo (°C) |
| `reading_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |
| `reading_date` | DATE | - | INDEX | **GENERATED** | Data (calculada) |

**Coluna Gerada:**
```sql
reading_date DATE GENERATED ALWAYS AS (CAST(reading_timestamp AS DATE)) STORED
```

**Índices:**
- `PRIMARY KEY (id)`
- `KEY idx_config_timestamp (config_id, reading_timestamp DESC)`
- `KEY idx_timestamp (reading_timestamp DESC)`
- `KEY idx_config_date (config_id, reading_date)`
- `KEY idx_readings_config_date (config_id, reading_date DESC)`

**Foreign Keys:**
- `FOREIGN KEY (config_id) REFERENCES configurations(id) ON DELETE SET NULL`

**Retenção:** ~500 registros por config_id (limpeza automática)

---

### 5. `controller_states` - Estados do Controlador

Histórico de acionamento de cooler/heater.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | SIM | FK, INDEX | NULL | Fermentação |
| `setpoint` | DECIMAL(4,1) | NÃO | - | - | Temp alvo |
| `cooling` | TINYINT(1) | SIM | - | 0 | Cooler ligado |
| `heating` | TINYINT(1) | SIM | - | 0 | Heater ligado |
| `state_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Índices:**
- `PRIMARY KEY (id)`
- `KEY idx_timestamp (state_timestamp DESC)`
- `KEY idx_controller_config_time (config_id, state_timestamp DESC)`
- `KEY idx_config_latest (config_id, state_timestamp DESC)`

**Retenção:** ~200 registros por config_id

---

### 6. `fermentation_states` - Estados da Fermentação (JSON)

Snapshots completos do estado da fermentação.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | NÃO | FK, INDEX | - | Fermentação |
| `state_data` | LONGTEXT (JSON) | SIM | - | NULL | Estado completo |
| `state_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Estrutura de `state_data` (após descompressão):**
```json
{
  "config_name": "IPA 2026",
  "config_id": 15,
  "currentStageIndex": 1,
  "totalStages": 4,
  "stageTargetTemp": 18.5,
  "pidTargetTemp": 18.5,
  "currentTargetTemp": 18.5,
  "stageType": "temperature",
  "cooling": false,
  "heating": true,
  "status": "running",
  "message": "Executando",
  "targetReached": true,
  "timeRemaining": {
    "days": 2,
    "hours": 14,
    "minutes": 30,
    "unit": "detailed",
    "status": "running"
  },
  "rampProgress": null,
  "timestamp": 1706360400000,
  "uptime_ms": 3600000
}
```

**Formato Comprimido (enviado pelo ESP):**
```json
{
  "cn": "IPA 2026",
  "cid": 15,
  "csi": 1,
  "ts": 4,
  "stt": 18.5,
  "ptt": 18.5,
  "ctt": 18.5,
  "st": "t",
  "c": false,
  "h": true,
  "s": "run",
  "msg": "r",
  "tr": [2, 14, 30, "r"],
  "tms": 1706360400000
}
```

**Mapeamento de Compressão:**
| Comprimido | Expandido |
|------------|-----------|
| `cn` | config_name |
| `csi` | currentStageIndex |
| `ts` | totalStages |
| `stt` | stageTargetTemp |
| `ptt` | pidTargetTemp |
| `ctt` | currentTargetTemp |
| `st` | stageType |
| `c` | cooling |
| `h` | heating |
| `s` | status |
| `msg` | message |
| `tr` | timeRemaining / targetReached |
| `rp` | rampProgress |
| `tms` | timestamp |
| `um` | uptime_ms |

**Mapeamento de Tipos de Etapa:**
| Comprimido | Expandido |
|------------|-----------|
| `t` | temperature |
| `r` | ramp |
| `g` | gravity |
| `gt` | gravity_time |

**Mapeamento de Status/Mensagens:**
| Comprimido | Expandido |
|------------|-----------|
| `r` / `run` | running / Executando |
| `w` / `wait` | waiting / Aguardando |
| `c` / `cool` | Resfriando |
| `h` / `heat` | Aquecendo |
| `i` / `idle` | Ocioso |
| `wg` | waiting_gravity |
| `tc` | completed / Fermentação concluída |
| `fc` / `fconc` | Fermentação concluída automaticamente |
| `targ` | Temperatura alvo atingida |
| `strt` | Etapa iniciada |

**Retenção:** ~100 registros por config_id

---

### 7. `esp_heartbeat` - Heartbeat do ESP8266

Sinais de vida do controlador para monitorar conectividade.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | NÃO | INDEX | - | Fermentação |
| `status` | VARCHAR(20) | SIM | - | 'online' | Status |
| `uptime_seconds` | INT(11) | SIM | - | 0 | Tempo ligado |
| `free_heap` | INT(11) | SIM | - | NULL | Heap livre (bytes) |
| `temp_fermenter` | DECIMAL(5,2) | SIM | - | NULL | Temp fermentador |
| `temp_fridge` | DECIMAL(5,2) | SIM | - | NULL | Temp geladeira |
| `cooler_active` | TINYINT(1) | SIM | - | NULL | Cooler ON |
| `heater_active` | TINYINT(1) | SIM | - | NULL | Heater ON |
| `control_status` | LONGTEXT (JSON) | SIM | - | NULL | Status detalhado |
| `heartbeat_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Estrutura de `control_status`:**
```json
{
  "state": "Resfriando",
  "is_waiting": true,
  "wait_reason": "Compressor Delay",
  "wait_seconds": 180,
  "wait_display": "3:00",
  "peak_detection": false,
  "estimated_peak": null
}
```

**Limiares:**
- **Offline:** > 120 segundos sem heartbeat
- **Heap Warning:** < 30.000 bytes
- **Heap Critical:** < 15.000 bytes

**Retenção:** ~50 registros por config_id

---

### 8. `ispindel_readings` - Leituras do iSpindel

Dados do hidrometro digital iSpindel.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | SIM | FK, INDEX | NULL | Fermentação |
| `name` | VARCHAR(50) | SIM | INDEX | 'iSpindel' | Nome do dispositivo |
| `temperature` | DECIMAL(4,1) | NÃO | - | - | Temp do líquido (°C) |
| `gravity` | DECIMAL(5,3) | NÃO | - | - | Gravidade específica |
| `battery` | DECIMAL(5,2) | SIM | - | NULL | Tensão bateria (V) |
| `reading_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Limiares:**
- **Stale (obsoleto):** > 3600 segundos (1 hora) sem leitura

**Retenção:** ~500 registros por config_id

---

### 9. `fermentation_events` - Eventos da Fermentação

Log de eventos importantes durante a fermentação.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | NÃO | FK, INDEX | - | Fermentação |
| `event_type` | VARCHAR(50) | NÃO | INDEX | - | Tipo do evento |
| `event_data` | LONGTEXT (JSON) | SIM | - | NULL | Dados do evento |
| `event_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Tipos de Evento:**
- `target_reached` - Temperatura alvo atingida
- `stage_completed` - Etapa concluída
- `stage_started` - Etapa iniciada
- `paused` - Fermentação pausada
- `resumed` - Fermentação retomada

---

### 10. `system_config` - Configurações Globais

Armazenamento key-value para configurações do sistema.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_key` | VARCHAR(50) | NÃO | UNIQUE | - | Chave |
| `config_value` | TEXT | NÃO | - | - | Valor |
| `updated_at` | TIMESTAMP | SIM | - | ON UPDATE | Atualização |

**Chaves Utilizadas:**
| Chave | Descrição | Exemplo |
|-------|-----------|---------|
| `sensor_fermentador` | Endereço DS18B20 fermentador | `28:FF:A1:B2:C3:D4:E5:01` |
| `sensor_geladeira` | Endereço DS18B20 geladeira | `28:FF:F1:E2:D3:C4:B5:02` |

---

### 11. `devices` - Dispositivos (Cache)

Cache de dispositivos e dados temporários.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `device_id` | VARCHAR(50) | NÃO | UNIQUE | - | Identificador |
| `device_type` | VARCHAR(20) | NÃO | INDEX | - | Tipo |
| `sensor_data` | TEXT (JSON) | SIM | - | NULL | Dados |
| `last_seen` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Último acesso |
| `is_online` | TINYINT(1) | SIM | - | 1 | Online |

**device_id especiais:**
| device_id | Descrição |
|-----------|-----------|
| `esp8266_main` | Controlador principal |
| `detected_sensors` | Lista de sensores OneWire |
| `current_temperatures` | Cache de temperaturas |
| `scan_command` | Comando de scan |

---

### 12. `detected_sensors` - Sensores OneWire Detectados

Lista de sensores DS18B20 encontrados no barramento.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `address` | VARCHAR(20) | NÃO | UNIQUE | - | Endereço do sensor |
| `detected_at` | TIMESTAMP | SIM | - | CURRENT_TIMESTAMP | Primeira detecção |
| `last_seen` | TIMESTAMP | SIM | INDEX | ON UPDATE | Última detecção |

---

### 13. `esp_commands` - Fila de Comandos para ESP

Comandos pendentes que o ESP deve buscar e executar.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `command` | VARCHAR(50) | NÃO | - | - | Comando |
| `status` | ENUM | SIM | INDEX | 'pending' | Estado |
| `created_at` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Criação |
| `executed_at` | TIMESTAMP | SIM | - | NULL | Execução |

**Comandos Disponíveis:**
| Comando | Descrição |
|---------|-----------|
| `CLEAR_EEPROM` | Limpa EEPROM (após trocar sensor) |

**Status:**
| Valor | Descrição |
|-------|-----------|
| `pending` | Aguardando execução |
| `executed` | Executado com sucesso |
| `failed` | Falhou |

---

### 14. `action_history` - Histórico de Ações

Log de auditoria de ações no sistema.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `user_id` | INT(11) | SIM | FK, INDEX | NULL | Usuário |
| `config_id` | INT(11) | SIM | FK, INDEX | NULL | Fermentação |
| `action_type` | VARCHAR(50) | NÃO | INDEX | - | Tipo |
| `action_details` | TEXT (JSON) | SIM | - | NULL | Detalhes |
| `action_timestamp` | TIMESTAMP | SIM | INDEX | CURRENT_TIMESTAMP | Momento |

**Tipos de Ação:**
- `create` - Configuração criada
- `start` - Fermentação iniciada
- `pause` - Fermentação pausada
- `resume` - Fermentação retomada
- `complete` - Fermentação concluída
- `delete` - Configuração excluída
- `user_registration` - Novo usuário
- `stage_advanced` - Etapa avançou
- `target_reached` - Alvo atingido

---

### 15. `alerts` - Alertas do Sistema

Notificações e alertas gerados pelo sistema.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `config_id` | INT(11) | SIM | FK, INDEX | NULL | Fermentação |
| `alert_type` | ENUM | NÃO | INDEX | - | Tipo |
| `alert_level` | ENUM | SIM | - | 'warning' | Nível |
| `message` | TEXT | NÃO | - | - | Mensagem |
| `is_read` | TINYINT(1) | SIM | INDEX | 0 | Lido |
| `created_at` | TIMESTAMP | SIM | - | CURRENT_TIMESTAMP | Criação |
| `resolved_at` | TIMESTAMP | SIM | - | NULL | Resolução |

**Tipos de Alerta:**
- `temperature` - Desvio de temperatura
- `gravity` - Alerta de gravidade
- `device` - Problema com dispositivo
- `stage_completion` - Etapa concluída
- `error` - Erro genérico

**Níveis:**
- `info` - Informativo
- `warning` - Atenção
- `critical` - Crítico

---

### 16. `sensor_cache` - Cache de Temperaturas

Cache de temperaturas atuais para acesso rápido.

| Campo | Tipo | Nulo | Índice | Default | Descrição |
|-------|------|------|--------|---------|-----------|
| `id` | INT(11) | NÃO | PK, AI | - | ID único |
| `sensor_type` | ENUM | NÃO | UNIQUE | - | Tipo: 'fermenter' ou 'fridge' |
| `temperature` | DECIMAL(4,1) | SIM | - | NULL | Temperatura (°C) |
| `updated_at` | TIMESTAMP | SIM | - | ON UPDATE | Atualização |

---

## Views (Visões)

### `v_active_fermentation`
Fermentação ativa com informações básicas.

```sql
SELECT c.id, c.name, c.status, c.current_stage_index, 
       c.current_target_temp, c.started_at,
       u.email AS user_email,
       COUNT(s.id) AS total_stages
FROM configurations c
LEFT JOIN users u ON c.user_id = u.id
LEFT JOIN stages s ON c.id = s.config_id
WHERE c.status = 'active'
GROUP BY c.id, ...
```

### `v_active_fermentation_complete`
Fermentação ativa com todos os dados atuais (temperaturas, gravidade, relés).

### `v_config_stats`
Estatísticas de cada configuração (min/max/avg temperatura, gravidade, duração).

### `v_latest_reading`
Última leitura de temperatura com nome da configuração.

### `v_sensors_config`
Sensores configurados (fermentador e geladeira).

```sql
SELECT config_key AS sensor_role, 
       config_value AS sensor_address, 
       updated_at
FROM system_config
WHERE config_key IN ('sensor_fermentador', 'sensor_geladeira')
AND config_value <> ''
```

---

## Política de Retenção de Dados

| Tabela | Registros por config_id | Cleanup Automático |
|--------|------------------------|-------------------|
| `readings` | 200-500 | Sim (cleanupOldRecords) |
| `controller_states` | 200 | Sim |
| `fermentation_states` | 100 | Sim |
| `esp_heartbeat` | 50 | Sim |
| `ispindel_readings` | 500 | Sim |

**Função `cleanupOldRecords($pdo, $table, $configId, $keepCount, $timestampCol)`:**
1. Conta registros do config_id específico
2. Se quantidade > (keepCount + 50), executa limpeza
3. Encontra timestamp de corte (registro na posição keepCount)
4. DELETE WHERE config_id = ? AND timestamp < cutoff LIMIT 1000
5. A cada 20 chamadas, limpa órfãos (config_id IS NULL)

**Limpeza Emergencial (`/api.php?path=emergency-cleanup`):**
- Requer secret key: `ferment2024cleanup`
- Limpa todas as tabelas de uma vez
- Remove órfãos e excesso de registros
