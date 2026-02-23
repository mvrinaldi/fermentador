# Documentação de API - Endpoints

**Base URL:** `https://seu-dominio.com/`  
**Formato:** JSON  
**Autenticação:** Sessão PHP (cookie)

---

## Índice de Endpoints

### API Principal (`/api.php?path=`)
| Método | Endpoint | Descrição |
|--------|----------|-----------|
| POST | `auth/login` | Login |
| POST | `auth/logout` | Logout |
| GET | `auth/check` | Verificar sessão |
| GET | `configurations` | Listar configurações |
| POST | `configurations` | Criar configuração |
| PUT | `configurations/status` | Alterar status |
| DELETE | `configurations/delete` | Excluir configuração |
| GET | `active` | Fermentação ativa |
| POST | `active/activate` | Ativar fermentação |
| POST | `active/deactivate` | Desativar fermentação |
| GET | `latest-readings` | Últimas leituras |
| GET | `state/complete` | Estado completo |
| POST | `readings` | Salvar leituras |
| POST | `ispindel/data` | Dados iSpindel |
| POST | `control` | Estado controlador |
| POST | `fermentation-state` | Estado fermentação |
| POST | `heartbeat` | Heartbeat ESP |
| POST | `cleanup` | Limpeza manual |
| POST | `emergency-cleanup` | Limpeza emergencial |

### API ESP (`/api/esp/`)
| Método | Endpoint | Descrição |
|--------|----------|-----------|
| GET | `active.php` | Fermentação ativa (ESP) |
| GET | `config.php?id=X` | Configuração para ESP |
| POST | `heartbeat.php` | Heartbeat ESP |
| GET/POST | `sensors.php` | Gerenciamento sensores |
| POST | `stage.php` | Atualizar etapa |
| POST | `state.php` | Salvar estado |
| POST | `target.php` | Notificar alvo |

---

## Autenticação

### POST `/api.php?path=auth/login`

Autentica o usuário e inicia sessão.

**Request:**
```json
{
  "email": "usuario@email.com",
  "password": "senha123"
}
```

**Response (200 OK):**
```json
{
  "success": true,
  "user_id": 1
}
```

**Response (401 Unauthorized):**
```json
{
  "error": "Credenciais inválidas"
}
```

---

### POST `/api.php?path=auth/logout`

Encerra a sessão do usuário.

**Request:** (vazio)

**Response (200 OK):**
```json
{
  "success": true
}
```

---

### GET `/api.php?path=auth/check`

Verifica se o usuário está autenticado.

**Response (autenticado):**
```json
{
  "authenticated": true,
  "user_id": 1
}
```

**Response (não autenticado):**
```json
{
  "authenticated": false
}
```

---

## Configurações

### GET `/api.php?path=configurations`

Lista todas as configurações do usuário.

**🔒 Requer Autenticação**

**Response (200 OK):**
```json
[
  {
    "id": 15,
    "user_id": 1,
    "name": "IPA 2026",
    "status": "active",
    "current_stage_index": 1,
    "current_target_temp": "18.5",
    "started_at": "2026-01-20 10:00:00",
    "paused_at": null,
    "completed_at": null,
    "times_used": 2,
    "created_at": "2026-01-15 14:30:00",
    "updated_at": "2026-01-27 08:00:00",
    "stage_count": 4,
    "stages": [
      {
        "id": 45,
        "config_id": 15,
        "stage_index": 0,
        "type": "temperature",
        "target_temp": "20.00",
        "duration": "3.00",
        "target_gravity": null,
        "max_duration": null,
        "start_temp": null,
        "ramp_time": null,
        "actual_rate": null,
        "direction": null,
        "status": "completed",
        "start_time": "2026-01-20 10:00:00",
        "end_time": "2026-01-23 10:00:00"
      },
      {
        "id": 46,
        "config_id": 15,
        "stage_index": 1,
        "type": "temperature",
        "target_temp": "18.50",
        "duration": "7.00",
        "status": "running",
        "start_time": "2026-01-23 10:00:00"
      }
    ]
  }
]
```

---

### POST `/api.php?path=configurations`

Cria uma nova configuração de fermentação.

**🔒 Requer Autenticação**

**Request:**
```json
{
  "name": "Pilsen 2026",
  "stages": [
    {
      "type": "temperature",
      "target_temp": 12,
      "duration": 14
    },
    {
      "type": "ramp",
      "start_temp": 12,
      "target_temp": 4,
      "ramp_time": 48
    },
    {
      "type": "temperature",
      "target_temp": 4,
      "duration": 7
    }
  ]
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "config_id": 16
}
```

**Campos por Tipo de Etapa:**

| Tipo | Campos Obrigatórios | Campos Opcionais |
|------|---------------------|------------------|
| `temperature` | type, target_temp, duration | - |
| `ramp` | type, start_temp, target_temp, ramp_time | direction, actual_rate |
| `gravity` | type, target_temp, target_gravity | - |
| `gravity_time` | type, target_temp, target_gravity, max_duration | - |

---

### PUT `/api.php?path=configurations/status`

Altera o status de uma configuração.

**🔒 Requer Autenticação**

**Request:**
```json
{
  "config_id": 15,
  "status": "active"
}
```

**Status disponíveis:** `active`, `paused`, `completed`

**Ações automáticas por status:**

| Status | Ações Executadas |
|--------|------------------|
| `active` | Define started_at, limpa paused_at/completed_at, reseta stages, incrementa times_used, **LIMPA DADOS ANTIGOS** (readings, ispindel, controller_states, fermentation_states, heartbeat) |
| `paused` | Define paused_at |
| `completed` | Define completed_at |

**Response (200 OK):**
```json
{
  "success": true
}
```

---

### DELETE `/api.php?path=configurations/delete`

Exclui uma configuração (não pode ser ativa).

**🔒 Requer Autenticação**

**Request:**
```json
{
  "config_id": 15
}
```

**Response (200 OK):**
```json
{
  "success": true
}
```

**Response (400 Bad Request):**
```json
{
  "error": "Não é possível excluir fermentação ativa"
}
```

---

## Fermentação Ativa

### GET `/api.php?path=active`

Retorna a fermentação ativa do usuário.

**🔒 Requer Autenticação**

**Response (com fermentação ativa):**
```json
{
  "active": true,
  "id": 15,
  "name": "IPA 2026",
  "currentStageIndex": 1
}
```

**Response (sem fermentação ativa):**
```json
{
  "active": false,
  "id": null
}
```

---

### GET `/api.php?path=state/complete`

Retorna estado completo da fermentação para o frontend.

**🔒 Requer Autenticação**

**Query Params:**
- `config_id` (obrigatório): ID da configuração

**Response (200 OK):**
```json
{
  "config": {
    "id": 15,
    "name": "IPA 2026",
    "status": "active",
    "current_stage_index": 1,
    "stages": [...]
  },
  "state": {
    "config_name": "IPA 2026",
    "currentStageIndex": 1,
    "totalStages": 4,
    "stageTargetTemp": 18.5,
    "pidTargetTemp": 18.5,
    "currentTargetTemp": 18.5,
    "cooling": false,
    "heating": false,
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
    "stageType": "temperature"
  },
  "readings": [
    {
      "id": 1234,
      "config_id": 15,
      "temp_fridge": "5.5",
      "temp_fermenter": "18.4",
      "temp_target": "18.5",
      "reading_timestamp": "2026-01-27 08:00:00"
    }
  ],
  "ispindel": {
    "id": 567,
    "config_id": 15,
    "name": "iSpindel",
    "temperature": "18.3",
    "gravity": "1.025",
    "battery": "4.12",
    "reading_timestamp": "2026-01-27 07:45:00",
    "is_stale": false,
    "seconds_since_update": 900
  },
  "ispindel_readings": [...],
  "controller": {
    "id": 890,
    "config_id": 15,
    "setpoint": "18.5",
    "cooling": 0,
    "heating": 0,
    "state_timestamp": "2026-01-27 08:00:00"
  },
  "controller_history": [...],
  "heartbeat": {
    "id": 111,
    "config_id": 15,
    "uptime_seconds": 86400,
    "free_heap": 35000,
    "temp_fermenter": "18.4",
    "temp_fridge": "5.5",
    "cooler_active": 0,
    "heater_active": 0,
    "control_status": {...},
    "heartbeat_timestamp": "2026-01-27 08:00:00"
  },
  "is_online": true,
  "timestamp": "2026-01-27 08:00:30"
}
```

---

## Endpoints para ESP8266

### POST `/api.php?path=readings`

Salva leituras de temperatura dos sensores.

**🔓 Não requer autenticação (chamado pelo ESP)**

**Request (formato completo):**
```json
{
  "config_id": 15,
  "temp_fridge": 5.5,
  "temp_fermenter": 18.4,
  "temp_target": 18.5
}
```

**Request (formato comprimido):**
```json
{
  "cid": 15,
  "tf": 5.5,
  "tb": 18.4,
  "tt": 18.5
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "reading_id": 1234
}
```

---

### POST `/api.php?path=heartbeat`

Envia heartbeat do ESP para monitoramento.

**🔓 Não requer autenticação**

**Request:**
```json
{
  "config_id": 15,
  "uptime": 86400,
  "free_heap": 35000,
  "temp_fermenter": 18.4,
  "temp_fridge": 5.5,
  "control_status": {
    "state": "idle",
    "is_waiting": false,
    "wait_reason": null,
    "wait_seconds": 0
  }
}
```

**Response (201 Created):**
```json
{
  "success": true
}
```

---

### POST `/api.php?path=fermentation-state`

Salva snapshot do estado da fermentação.

**🔓 Não requer autenticação**

**Request (formato comprimido):**
```json
{
  "cid": 15,
  "cn": "IPA 2026",
  "csi": 1,
  "ts": 4,
  "stt": 18.5,
  "ptt": 18.5,
  "ctt": 18.5,
  "st": "t",
  "c": false,
  "h": false,
  "s": "run",
  "msg": "r",
  "tr": [2, 14, 30, "r"],
  "tms": 1706360400000
}
```

**Response (201 Created):**
```json
{
  "success": true
}
```

---

### POST `/api.php?path=ispindel/data`

Recebe dados do iSpindel.

**🔓 Não requer autenticação**

**Request:**
```json
{
  "name": "iSpindel",
  "temperature": 18.3,
  "gravity": 1.025,
  "battery": 4.12,
  "config_id": 15
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "message": "iSpindel data saved",
  "config_id": 15
}
```

**Nota:** Se `config_id` não for fornecido, busca fermentação ativa automaticamente.

---

### POST `/api.php?path=control`

Salva estado do controlador (cooling/heating).

**🔓 Não requer autenticação**

**Request:**
```json
{
  "config_id": 15,
  "setpoint": 18.5,
  "cooling": false,
  "heating": true
}
```

**Response (201 Created):**
```json
{
  "success": true
}
```

---

## API ESP Dedicada

### GET `/api/esp/active.php`

Retorna fermentação ativa (simplificado para ESP).

**Response (com fermentação):**
```json
{
  "active": true,
  "id": "15",
  "name": "IPA 2026",
  "status": "active",
  "currentStageIndex": 1
}
```

**Response (sem fermentação):**
```json
{
  "active": false,
  "id": "",
  "message": "No active fermentation"
}
```

---

### GET `/api/esp/config.php?id={config_id}`

Retorna configuração otimizada para ESP.

**Response (200 OK):**
```json
{
  "name": "IPA 2026",
  "status": "active",
  "currentStageIndex": 1,
  "currentTargetTemp": 18.5,
  "stages": [
    {
      "type": "temperature",
      "targetTemp": 20.0,
      "startTemp": 0.0,
      "duration": 3.0,
      "rampTime": 0,
      "targetGravity": 0.0,
      "timeoutDays": 0.0,
      "status": "completed"
    },
    {
      "type": "temperature",
      "targetTemp": 18.5,
      "startTemp": 0.0,
      "duration": 7.0,
      "rampTime": 0,
      "targetGravity": 0.0,
      "timeoutDays": 0.0,
      "status": "running"
    }
  ]
}
```

---

### POST `/api/esp/stage.php`

Notifica avanço de etapa.

**Request:**
```json
{
  "config_id": 15,
  "currentStageIndex": 2
}
```

**Response (200 OK):**
```json
{
  "success": true,
  "config_id": 15,
  "previous_stage_index": 1,
  "current_stage_index": 2,
  "new_target_temp": 4.0
}
```

**Ações executadas:**
1. Marca etapa anterior como `completed`
2. Marca nova etapa como `running`
3. Atualiza `current_stage_index` na configuração
4. Atualiza `current_target_temp`
5. Registra no `action_history`

---

### POST `/api/esp/target.php`

Notifica que temperatura alvo foi atingida.

**Request:**
```json
{
  "config_id": 15,
  "target_reached": true
}
```

**Response (200 OK):**
```json
{
  "success": true
}
```

**Ações executadas:**
1. Atualiza `target_reached_time` na etapa atual
2. Registra no `action_history`

---

## Gerenciamento de Sensores

### GET `/api/esp/sensors.php?action=get_assigned`

Retorna sensores configurados.

**Response:**
```json
{
  "success": true,
  "sensors": {
    "sensor_fermentador": "28:FF:A1:B2:C3:D4:E5:01",
    "sensor_geladeira": "28:FF:F1:E2:D3:C4:B5:02"
  },
  "message": "Sensors found"
}
```

---

### GET `/api/esp/sensors.php?action=get_detected`

Retorna sensores detectados no barramento OneWire.

**Response:**
```json
{
  "success": true,
  "sensors": [
    "28:FF:A1:B2:C3:D4:E5:01",
    "28:FF:F1:E2:D3:C4:B5:02",
    "28:FF:11:22:33:44:55:03"
  ],
  "count": 3
}
```

---

### POST `/api/esp/sensors.php?action=save_detected`

Salva lista de sensores detectados pelo ESP.

**Request:**
```json
{
  "sensors": [
    "28:FF:A1:B2:C3:D4:E5:01",
    "28:FF:F1:E2:D3:C4:B5:02"
  ]
}
```

**Response:**
```json
{
  "success": true,
  "count": 2,
  "message": "Sensors saved successfully"
}
```

---

### POST `/api/esp/sensors.php?action=assign`

Atribui sensor a uma função.

**Request:**
```json
{
  "address": "28:FF:A1:B2:C3:D4:E5:01",
  "role": "sensor_fermentador"
}
```

**Roles válidos:** `sensor_fermentador`, `sensor_geladeira`

**Response:**
```json
{
  "success": true,
  "updated": 1,
  "clear_eeprom_sent": true,
  "message": "1 sensor(s) configured. EEPROM clear command sent."
}
```

**Nota:** Automaticamente envia comando `CLEAR_EEPROM` para o ESP recarregar sensores.

---

### GET `/api/esp/sensors.php?action=get_commands`

Busca comandos pendentes (chamado pelo ESP).

**Response:**
```json
{
  "success": true,
  "commands": [
    {
      "id": 5,
      "command": "CLEAR_EEPROM",
      "created_at": "2026-01-27 08:00:00"
    }
  ],
  "count": 1
}
```

---

### POST `/api/esp/sensors.php?action=mark_executed`

Marca comando como executado (chamado pelo ESP).

**Request:**
```json
{
  "command_id": 5
}
```

**Response:**
```json
{
  "success": true,
  "message": "Command marked as executed"
}
```

---

### POST `/api/esp/sensors.php?action=update_temperatures`

Atualiza cache de temperaturas (chamado pelo ESP).

**Request:**
```json
{
  "temp_fermenter": 18.4,
  "temp_fridge": 5.5
}
```

**Response:**
```json
{
  "success": true,
  "message": "Temperatures updated"
}
```

---

### GET `/api/esp/sensors.php?action=get_temperatures`

Retorna temperaturas do cache (para página de sensores).

**Response:**
```json
{
  "success": true,
  "temperatures": {
    "fermenter": 18.4,
    "fridge": 5.5
  },
  "timestamp": "2026-01-27 08:00:00",
  "source": "cache"
}
```

---

## Códigos de Erro

| Código | Significado | Quando ocorre |
|--------|-------------|---------------|
| 200 | OK | Sucesso (GET, PUT) |
| 201 | Created | Sucesso (POST) |
| 400 | Bad Request | Dados inválidos/faltando |
| 401 | Unauthorized | Não autenticado |
| 404 | Not Found | Recurso não encontrado |
| 500 | Internal Error | Erro no servidor/banco |

**Formato de erro:**
```json
{
  "error": "Descrição do erro",
  "require_login": true  // opcional, indica necessidade de login
}
```

---

## Compressão de Dados (ESP → Servidor)

Para economizar memória e banda do ESP8266, alguns campos usam nomes curtos:

### Campos
| Curto | Completo |
|-------|----------|
| `cid` | config_id |
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
| `tr` | timeRemaining/targetReached |
| `rp` | rampProgress |
| `tf` | temp_fridge |
| `tb` | temp_fermenter (beer) |
| `tt` | temp_target |
| `um` | uptime_ms |
| `tms` | timestamp |

### Valores de timeRemaining (`tr`)
| Formato | Significado |
|---------|-------------|
| `[2, 14, 30, "r"]` | 2d 14h 30m, status "running" |
| `[5.5, "h", "r"]` | 5.5 horas, running |
| `[3, "d", "w"]` | 3 dias, waiting |
| `["tc"]` ou `"tc"` | completed |
| `true/false` | targetReached (sem tempo) |

### Status/Mensagens
| Curto | Completo |
|-------|----------|
| `r`/`run` | running/Executando |
| `w`/`wait` | waiting/Aguardando |
| `c`/`cool` | Resfriando |
| `h`/`heat` | Aquecendo |
| `i`/`idle` | Ocioso |
| `wg` | waiting_gravity |
| `tc` | completed |
| `fc`/`fconc` | Fermentação concluída |

### Tipos de Etapa
| Curto | Completo |
|-------|----------|
| `t` | temperature |
| `r` | ramp |
| `g` | gravity |
| `gt` | gravity_time |

---

## Fluxo de Comunicação ESP ↔ Servidor

```
┌─────────────────────────────────────────────────────────────────────┐
│                    CICLO PRINCIPAL DO ESP (30s)                     │
└─────────────────────────────────────────────────────────────────────┘

  ESP8266                                 Servidor
     │                                       │
     │  1. GET /api/esp/active.php           │
     │  ─────────────────────────────────►   │
     │  ◄───────────── {active, id} ─────────│
     │                                       │
     │  2. GET /api/esp/config.php?id=X      │
     │  ─────────────────────────────────►   │
     │  ◄──────── {stages, target...} ───────│
     │                                       │
     │  [Loop de Leituras - a cada 30s]      │
     │                                       │
     │  3. POST /api.php?path=readings       │
     │     {cid, tf, tb, tt}                 │
     │  ─────────────────────────────────►   │
     │  ◄──────────── {success} ─────────────│
     │                                       │
     │  4. POST /api.php?path=heartbeat      │
     │     {cid, uptime, heap, temps...}     │
     │  ─────────────────────────────────►   │
     │  ◄──────────── {success} ─────────────│
     │                                       │
     │  5. POST /api.php?path=fermentation-state
     │     {estado completo comprimido}      │
     │  ─────────────────────────────────►   │
     │  ◄──────────── {success} ─────────────│
     │                                       │
     │  [Se etapa concluída]                 │
     │                                       │
     │  6. POST /api/esp/stage.php           │
     │     {config_id, currentStageIndex}    │
     │  ─────────────────────────────────►   │
     │  ◄─── {success, new_target_temp} ─────│
     │                                       │


┌─────────────────────────────────────────────────────────────────────┐
│                    CICLO DO ISPINDEL (15-60 min)                    │
└─────────────────────────────────────────────────────────────────────┘

  iSpindel                                Servidor
     │                                       │
     │  POST /api.php?path=ispindel/data     │
     │  {name, temp, gravity, battery}       │
     │  ─────────────────────────────────►   │
     │  ◄──────────── {success} ─────────────│
     │                                       │
     │  [Volta a dormir]                     │


┌─────────────────────────────────────────────────────────────────────┐
│                    CICLO DO FRONTEND (30s)                          │
└─────────────────────────────────────────────────────────────────────┘

  Frontend                                Servidor
     │                                       │
     │  GET /api.php?path=state/complete     │
     │      &config_id=X                     │
     │  ─────────────────────────────────►   │
     │                                       │
     │  ◄──── {config, state, readings, ─────│
     │         ispindel, controller,         │
     │         heartbeat, is_online}         │
     │                                       │
     │  [Renderiza UI, atualiza gráfico]     │
```
