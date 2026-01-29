# Fluxos de Dados e Diagramas de Sequência

Este documento detalha como os dados fluem através do sistema.

---

## 1. Visão Geral do Fluxo

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        CAMADA FÍSICA                                    │
│   DS18B20 (Ferment.) ──┬── DS18B20 (Geladeira)     iSpindel             │
│                        │                              │                 │
│                   ┌────▼────┐                         │                 │
│                   │ OneWire │                         │                 │
│                   └────┬────┘                         │                 │
│                        │                              │                 │
│                   ┌────▼─────────────────────────────▼────┐             │
│                   │              ESP8266                   │            │
│                   │  • Leitura sensores                    │            │
│                   │  • Algoritmo PID                       │            │
│                   │  • Lógica de etapas                    │            │
│                   │  • Controle relés (Cooler/Heater)      │            │
│                   └────┬──────────────────────────────────┘             │
│                        │                                                │
│                   Relé Cooler ──► Geladeira                             │
│                   Relé Heater ──► Aquecedor                             │
└────────────────────────┼────────────────────────────────────────────────┘
                         │ WiFi/Internet
                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        SERVIDOR (PHP/MySQL)                             │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  API Layer (api.php + api/esp/*.php)                            │   │
│   │  • Recebe dados do ESP                                          │   │
│   │  • Descomprime JSON                                             │   │
│   │  • Valida e persiste                                            │   │
│   │  • Cleanup automático                                           │   │
│   └──────────────────────────┬──────────────────────────────────────┘   │
│                             │                                           │
│   ┌──────────────────────────▼──────────────────────────────────────┐   │
│   │  MySQL Database                                                 │   │
│   │  readings | states | heartbeat | ispindel | controller          │   │
│   └─────────────────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────────────────┘
                         │ HTTP/HTTPS
                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        FRONTEND (Browser)                               │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  JavaScript (app.js, config.js)                                 │   │
│   │  • Polling a cada 30s                                           │   │
│   │  • Descompressão local                                          │   │
│   │  • Gerenciamento de estado                                      │   │
│   │  • Renderização de UI                                           │   │
│   └──────────────────────────┬──────────────────────────────────────┘   │
│                              │                                          │
│   ┌──────────────────────────▼──────────────────────────────────────┐   │
│   │  DOM (Cards, Gráfico, Etapas, Status ESP, Alertas)              │   │
│   └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Fluxo: Leitura de Temperatura

```
  DS18B20          ESP8266              Servidor              MySQL
     │                │                    │                    │
     │  requestTemp() │                    │                    │
     │◄───────────────│                    │                    │
     │                │                    │                    │
     │  getTempC()    │                    │                    │
     │───────────────►│                    │                    │
     │   (°C)         │                    │                    │
     │                │  POST /readings    │                    │
     │                │  {cid,tf,tb,tt}    │                    │
     │                │───────────────────►│                    │
     │                │                    │  INSERT readings   │
     │                │                    │───────────────────►│
     │                │                    │  cleanup()         │
     │                │                    │───────────────────►│
     │                │  {success:true}    │                    │
     │                │◄───────────────────│                    │

Frequência: 30 segundos
Campos comprimidos: cid=config_id, tf=temp_fridge, tb=temp_beer, tt=temp_target
```

---

## 3. Fluxo: Controle PID

```
                ESP8266 (Loop 1s)
                      │
                      ▼
        ┌─────────────────────────────┐
        │  Lê temp fermentador        │
        │  (atual = 19.2°C)           │
        └──────────────┬──────────────┘
                       │
                       ▼
        ┌─────────────────────────────┐
        │  Calcula erro               │
        │  erro = alvo - atual        │
        │  erro = 18.5 - 19.2 = -0.7  │
        └──────────────┬──────────────┘
                       │
           ┌───────────┴───────────┐
           │                       │
           ▼                       ▼
    ┌─────────────┐         ┌─────────────┐
    │ erro < -0.3 │         │ erro > +0.3 │
    │ (quente)    │         │ (frio)      │
    └──────┬──────┘         └──────┬──────┘
           │                       │
           ▼                       ▼
    Liga COOLER              Liga HEATER
    Desliga HEATER           Desliga COOLER
           │                       │
           └───────────┬───────────┘
                       │
                       ▼
        ┌─────────────────────────────┐
        │  Verifica proteções:        │
        │  • Tempo mínimo ON (30s)    │
        │  • Tempo mínimo OFF (180s)  │
        │  • Delay compressor (5min)  │
        └──────────────┬──────────────┘
                       │
                       ▼
        ┌─────────────────────────────┐
        │  digitalWrite(PIN, state)   │
        └─────────────────────────────┘

Histerese padrão: ±0.3°C
```

---

## 4. Fluxo: Avanço de Etapa

```
  ESP8266                    Servidor                     MySQL
     │                          │                            │
     │  [Detecta fim etapa]     │                            │
     │  • TEMP: tempo >= dur    │                            │
     │  • RAMP: temp == alvo    │                            │
     │  • GRAVITY: grav <= alvo │                            │
     │                          │                            │
     │  POST /stage             │                            │
     │  {config_id, newIndex}   │                            │
     │─────────────────────────►│                            │
     │                          │  BEGIN TRANSACTION         │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  UPDATE stages             │
     │                          │  SET status='completed'    │
     │                          │  WHERE index=old           │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  UPDATE stages             │
     │                          │  SET status='running'      │
     │                          │  WHERE index=new           │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  UPDATE configurations     │
     │                          │  SET current_stage_idx     │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  INSERT action_history     │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  COMMIT                    │
     │                          │───────────────────────────►│
     │                          │                            │
     │  {success, new_target}   │                            │
     │◄─────────────────────────│                            │
     │                          │                            │
     │  Atualiza PID local      │                            │
```

**Tipos de Etapa:**
| Tipo | Condição de Término |
|------|---------------------|
| `temperature` | tempo_decorrido >= duration (dias) |
| `ramp` | temp_atual == target_temp |
| `gravity` | gravidade <= target_gravity |
| `gravity_time` | gravidade <= target OU tempo >= max_duration |

---

## 5. Fluxo: Atualização Frontend

```
  Frontend                   Servidor                     MySQL
     │                          │                            │
     │  [setInterval 30s]       │                            │
     │                          │                            │
     │  GET /state/complete     │                            │
     │  &config_id=15           │                            │
     │─────────────────────────►│                            │
     │                          │  SELECT config, stages     │
     │                          │───────────────────────────►│
     │                          │  SELECT last state         │
     │                          │───────────────────────────►│
     │                          │  SELECT readings 24h       │
     │                          │───────────────────────────►│
     │                          │  SELECT ispindel           │
     │                          │───────────────────────────►│
     │                          │  SELECT heartbeat          │
     │                          │───────────────────────────►│
     │                          │                            │
     │                          │  decompressStateData()     │
     │                          │                            │
     │  {config, state,         │                            │
     │   readings[], ispindel,  │                            │
     │   heartbeat, is_online}  │                            │
     │◄─────────────────────────│                            │
     │                          │                            │
     │  decompressData() JS     │                            │
     │  appState = {...}        │                            │
     │  renderUI()              │                            │
```

**Estado do App (appState):**
```javascript
{
    config: { id, name, status, stages: [...] },
    espState: { currentStageIndex, targetReached, timeRemaining, ... },
    readings: [...],
    ispindel: { gravity, temperature, battery, is_stale },
    ispindelReadings: [...],
    controller: { setpoint, cooling, heating },
    controllerHistory: [...],
    heartbeat: { uptime, free_heap, control_status },
    lastUpdate: "..."
}
```

---

## 6. Fluxo: iSpindel

```
  iSpindel                   Servidor                        
     │                          │                            
     │  [Acorda do sleep]       │                            
     │  Lê acelerômetro         │                            
     │  Lê temperatura          │                            
     │  Lê bateria              │                            
     │                          │                            
     │  POST /ispindel/data     │                            
     │  {name, temp, gravity,   │                            
     │   battery, config_id}    │                            
     │─────────────────────────►│                            
     │                          │  INSERT ispindel_readings  
     │                          │  cleanup()                 
     │  {success:true}          │                            
     │◄─────────────────────────│                            
     │                          │                            
     │  [Deep sleep 15-60min]   │                            

Limiar Stale: > 3600 segundos (1 hora) sem dados
```

---

## 7. Fluxo: Heartbeat e Monitoramento

```
  ESP8266                    Servidor                     Frontend
     │                          │                            │
     │  [A cada 30s]            │                            │
     │                          │                            │
     │  POST /heartbeat         │                            │
     │  {cid, uptime,           │                            │
     │   free_heap, temps,      │                            │
     │   control_status}        │                            │
     │─────────────────────────►│                            │
     │                          │  INSERT esp_heartbeat      │
     │  {success:true}          │                            │
     │◄─────────────────────────│                            │
     │                          │                            │
     │                          │  GET /state/complete       │
     │                          │◄───────────────────────────│
     │                          │                            │
     │                          │  Calcula is_online:        │
     │                          │  diff = now - timestamp    │
     │                          │  is_online = diff < 120s   │
     │                          │                            │
     │                          │  Response                  │
     │                          │────────────────────────────►
     │                          │                            │
     │                          │  checkESPStatus()          │
     │                          │  • Badge Online/Offline    │
     │                          │  • Status relés            │
     │                          │  • Alerta heap baixo       │
```

**Limiares:**
| Métrica | Valor | Resultado |
|---------|-------|-----------|
| Sem heartbeat | > 120s | Badge "ESP Offline" |
| free_heap | < 30KB | Alerta "Memória baixa" (amarelo) |
| free_heap | < 15KB | Alerta "Memória crítica" (vermelho) |
| iSpindel sem dados | > 1h | Badge "Stale" |

---

## 8. Fluxo: Iniciar Fermentação

```
  Frontend                   Servidor                     MySQL
     │                          │                            │
     │  1. Verifica se há ativa │                            │
     │  GET /active             │                            │
     │─────────────────────────►│                            │
     │  {active:false}          │                            │
     │◄─────────────────────────│                            │
     │                          │                            │
     │  2. Ativa fermentação    │                            │
     │  PUT /configurations/    │                            │
     │      status              │                            │
     │  {config_id, "active"}   │                            │
     │─────────────────────────►│                            │
     │                          │  BEGIN TRANSACTION         │
     │                          │                            │
     │                          │  UPDATE configurations     │
     │                          │  SET status='active',      │
     │                          │      started_at=NOW(),     │
     │                          │      current_stage_idx=0   │
     │                          │                            │
     │                          │  UPDATE stages             │
     │                          │  SET status='pending'      │
     │                          │  (todas)                   │
     │                          │                            │
     │                          │  UPDATE stages             │
     │                          │  SET status='running'      │
     │                          │  WHERE stage_index=0       │
     │                          │                            │
     │                          │    LIMPA DADOS ANTIGOS:    │
     │                          │  DELETE readings           │
     │                          │  DELETE ispindel_readings  │
     │                          │  DELETE controller_states  │
     │                          │  DELETE fermentation_states│
     │                          │  DELETE esp_heartbeat      │
     │                          │                            │
     │                          │  COMMIT                    │
     │  {success:true}          │                            │
     │◄─────────────────────────│                            │
```

---

## 9. Frequências de Comunicação

| Componente | Frequência | Endpoint |
|------------|------------|----------|
| ESP → Leituras | 30s | POST /readings |
| ESP → Heartbeat | 30s | POST /heartbeat |
| ESP → Estado | 30s | POST /fermentation-state |
| ESP → Etapa | Sob demanda | POST /stage |
| iSpindel | 15-60min | POST /ispindel/data |
| Frontend → Polling | 30s | GET /state/complete |
| ESP (boot) | Uma vez | GET /active, /config |

---

## 10. Estimativa de Volume

**Por fermentação de 14 dias (SEM cleanup):**
- readings: ~40.320 registros
- esp_heartbeat: ~40.320 registros
- ispindel: ~672-1344 registros
- fermentation_states: ~40.320 registros
- **TOTAL: ~120.000+ registros**

**Com cleanup automático:**
- readings: 500 registros
- esp_heartbeat: 50 registros
- ispindel: 500 registros
- fermentation_states: 100 registros
- **TOTAL: ~1.150 registros**

**Economia: ~99%**
