# Sistema de Controle de Fermentação - Documentação Técnica

**Versão:** 1.0  
**Data:** Janeiro 2026  
**Autor:** Marcos Rinaldi

---

## Índice de Documentos

| # | Documento | Descrição |
|---|-----------|-----------|
| 1 | [01-VISAO-GERAL.md](01-VISAO-GERAL.md) | Arquitetura, tecnologias, diagramas de sistema |
| 2 | [02-DICIONARIO-DADOS.md](02-DICIONARIO-DADOS.md) | Todas as tabelas MySQL, campos, tipos, índices |
| 3 | [03-API-ENDPOINTS.md](03-API-ENDPOINTS.md) | Endpoints REST, requests/responses, autenticação |
| 4 | [04-FLUXOS-DADOS.md](04-FLUXOS-DADOS.md) | Diagramas de sequência, fluxo sensor→frontend |
| 5 | [05-ANALISE-CHECKLIST.md](05-ANALISE-CHECKLIST.md) | Análise de implementação, checklists, recomendações |

---

## Resumo do Sistema

### O que é
Sistema IoT para monitoramento e controle automático de fermentação de cerveja artesanal.

### Componentes
- **ESP8266/ESP32**: Controlador com sensores DS18B20 e relés
- **iSpindel**: Hidrometro digital para gravidade
- **Servidor PHP/MySQL**: API REST e armazenamento
- **Frontend Web**: Dashboard de monitoramento

### Funcionalidades Principais
- Controle PID de temperatura
- 4 tipos de etapas (temperatura, rampa, gravidade, gravidade+tempo)
- Monitoramento em tempo real
- Histórico com gráficos
- Multi-usuário com autenticação

---

## Diagrama Simplificado

```
┌─────────────────────────────────────────────────────────────┐
│                      HARDWARE                                │
│  DS18B20 ──┬── DS18B20     iSpindel                         │
│            │                  │                              │
│       ┌────▼──────────────────▼────┐                        │
│       │        ESP8266             │                        │
│       │   PID + Lógica Etapas      │                        │
│       └────┬───────────────────────┘                        │
│            │                                                 │
│       Relés ──► Cooler / Heater                             │
└────────────┼────────────────────────────────────────────────┘
             │ WiFi
             ▼
┌─────────────────────────────────────────────────────────────┐
│                     SERVIDOR                                 │
│    ┌─────────────┐     ┌─────────────┐                      │
│    │   API PHP   │────►│   MySQL     │                      │
│    └─────────────┘     └─────────────┘                      │
└────────────┬────────────────────────────────────────────────┘
             │ HTTPS
             ▼
┌─────────────────────────────────────────────────────────────┐
│                     FRONTEND                                 │
│    Dashboard • Gráficos • Configuração • Monitoramento      │
└─────────────────────────────────────────────────────────────┘
```

---

## Banco de Dados - Visão Geral

### Tabelas Principais (14 tabelas + 5 views)

| Tabela | Propósito |
|--------|-----------|
| `users` | Autenticação |
| `configurations` | Receitas de fermentação |
| `stages` | Etapas de cada receita |
| `readings` | Histórico de temperatura |
| `controller_states` | Histórico de relés |
| `fermentation_states` | Estados completos (JSON) |
| `esp_heartbeat` | Monitoramento do ESP |
| `ispindel_readings` | Dados de gravidade |
| `system_config` | Configurações globais |
| `devices` | Cache de dispositivos |
| `detected_sensors` | Sensores OneWire |
| `esp_commands` | Fila de comandos |
| `action_history` | Log de auditoria |
| `alerts` | Sistema de alertas |

---

## API - Endpoints Principais

### Autenticação
| Método | Endpoint | Descrição |
|--------|----------|-----------|
| POST | `/api.php?path=auth/login` | Login |
| POST | `/api.php?path=auth/logout` | Logout |
| GET | `/api.php?path=auth/check` | Verificar sessão |

### Fermentação
| Método | Endpoint | Descrição |
|--------|----------|-----------|
| GET | `/api.php?path=configurations` | Listar |
| POST | `/api.php?path=configurations` | Criar |
| PUT | `/api.php?path=configurations/status` | Iniciar/Pausar/Concluir |
| GET | `/api.php?path=state/complete` | Estado completo |

### ESP8266
| Método | Endpoint | Descrição |
|--------|----------|-----------|
| POST | `/api.php?path=readings` | Enviar temperaturas |
| POST | `/api.php?path=heartbeat` | Heartbeat |
| POST | `/api.php?path=fermentation-state` | Estado |
| POST | `/api/esp/stage.php` | Avanço de etapa |

---

## Fluxo Principal

```
1. ESP8266 lê sensores DS18B20 (a cada segundo)
2. Algoritmo PID calcula se deve ligar Cooler ou Heater
3. A cada 30s, envia dados para o servidor:
   - POST /readings (temperaturas)
   - POST /heartbeat (status)
   - POST /fermentation-state (estado completo)
4. Servidor descomprime, valida e armazena no MySQL
5. Servidor executa cleanup automático (mantém últimos N registros)
6. Frontend faz polling a cada 30s (GET /state/complete)
7. Frontend descomprime dados e renderiza UI
8. Quando etapa termina, ESP avisa servidor (POST /stage)
```

---

## Status da Implementação

| Categoria | Status |
|-----------|--------|
| Autenticação | ✅ Completo |
| CRUD Configurações | ✅ Completo |
| Controle Fermentação | ✅ Completo |
| Monitoramento | ✅ Completo |
| Integração ESP | ✅ Completo |
| Integração iSpindel | ✅ Completo |
| Gerenciamento Sensores | ✅ Completo |
| Sistema de Alertas | ✅ Completo |
| Backup Automático | ❌ Não implementado |

---

## Contato

Para dúvidas sobre a documentação ou sistema, consultar os documentos detalhados ou o código-fonte.
