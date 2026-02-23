# Sistema de Controle de Fermentação - Documentação Técnica

**Versão:** 1.0  
**Última Atualização:** Janeiro 2026  
**Autor:** Marcos Rinaldi

---

## 1. Visão Geral do Sistema

### 1.1 Descrição

Sistema IoT completo para monitoramento e controle de fermentação de cerveja artesanal, composto por:

- **Controlador ESP8266/ESP32**: Leitura de sensores e controle de relés
- **Servidor PHP/MySQL**: API REST e armazenamento de dados
- **Frontend Web**: Dashboard de monitoramento em tempo real

### 1.2 Funcionalidades Principais

| Funcionalidade | Descrição |
|----------------|-----------|
| Controle de Temperatura | PID com dois sensores DS18B20 (fermentador + geladeira) |
| Etapas de Fermentação | Suporte a temperatura, rampa, gravidade e gravidade+tempo |
| Monitoramento Remoto | Dashboard web com gráficos em tempo real |
| Integração iSpindel | Leitura de gravidade via hidrometro digital |
| Histórico de Dados | Armazenamento de leituras para análise posterior |
| Multi-usuário | Sistema de autenticação com sessões |

### 1.3 Diagrama de Arquitetura

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           AMBIENTE FÍSICO                                    │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                   │
│  │   Sensor     │    │   Sensor     │    │   iSpindel   │                   │
│  │ DS18B20      │    │ DS18B20      │    │  (Gravidade) │                   │
│  │ (Fermentador)│    │ (Geladeira)  │    │              │                   │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘                   │
│         │                   │                    │                           │
│         └─────────┬─────────┘                    │                           │
│                   │                              │                           │
│         ┌─────────▼─────────┐                    │                           │
│         │     ESP8266       │◄───────────────────┘                           │
│         │  (Controlador)    │                                                │
│         │                   │                                                │
│         │  • PID Control    │                                                │
│         │  • Lógica Etapas  │                                                │
│         │  • Estado Local   │                                                │
│         └─────────┬─────────┘                                                │
│                   │                                                          │
│         ┌─────────▼─────────┐                                                │
│         │   Relé Cooler     │     ┌─────────────┐                           │
│         │   Relé Heater     │────►│  Geladeira  │                           │
│         └───────────────────┘     │  + Aquecedor│                           │
│                                   └─────────────┘                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ HTTP/HTTPS
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           SERVIDOR (Hostinger)                              │
│                                                                             │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                         API PHP                                       │  │
│  │                                                                       │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │  │
│  │  │   api.php   │  │ api/esp/    │  │ api/esp/    │  │ api/esp/    │ │  │
│  │  │  (Principal)│  │ active.php  │  │ config.php  │  │ sensors.php │ │  │
│  │  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘ │  │
│  │         │                │                │                │        │  │
│  │         └────────────────┴────────────────┴────────────────┘        │  │
│  │                                   │                                  │  │
│  └───────────────────────────────────┼──────────────────────────────────┘  │
│                                      │                                      │
│                                      ▼                                      │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                         MySQL Database                                │  │
│  │                                                                       │  │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────────────┐ │  │
│  │  │   users    │ │ configs    │ │  stages    │ │     readings       │ │  │
│  │  └────────────┘ └────────────┘ └────────────┘ └────────────────────┘ │  │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────────────┐ │  │
│  │  │ heartbeat  │ │ ferm_states│ │ ctrl_states│ │ ispindel_readings  │ │  │
│  │  └────────────┘ └────────────┘ └────────────┘ └────────────────────┘ │  │
│  │                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ HTTP/HTTPS
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           FRONTEND WEB                                      │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │
│  │ index.html  │  │ config.html │  │sensores.html│  │  cadastro.html  │   │
│  │  (Monitor)  │  │(Configurar) │  │  (Sensores) │  │   (Registro)    │   │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────┘   │
│         │                │                │                │               │
│         ▼                ▼                ▼                ▼               │
│  ┌─────────────┐  ┌─────────────┐                                         │
│  │   app.js    │  │  config.js  │         ← JavaScript SPA                │
│  └─────────────┘  └─────────────┘                                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.4 Fluxo de Dados Principal

```
┌────────────────────────────────────────────────────────────────────────────┐
│                        FLUXO: LEITURA DE TEMPERATURA                       │
└────────────────────────────────────────────────────────────────────────────┘

  ESP8266                    Servidor                     Frontend
     │                          │                            │
     │  1. Lê DS18B20           │                            │
     │  ─────────────►          │                            │
     │                          │                            │
     │  2. POST /readings       │                            │
     │  {tf, tb, tt, cid}       │                            │
     │  ─────────────────────►  │                            │
     │                          │  3. INSERT readings        │
     │                          │  ──────────►               │
     │                          │                            │
     │                          │  4. cleanupOldRecords()    │
     │                          │  ──────────►               │
     │                          │                            │
     │  5. {success: true}      │                            │
     │  ◄─────────────────────  │                            │
     │                          │                            │
     │                          │  6. GET /state/complete    │
     │                          │  ◄────────────────────────  │
     │                          │                            │
     │                          │  7. SELECT readings...     │
     │                          │  ──────────►               │
     │                          │                            │
     │                          │  8. JSON response          │
     │                          │  ────────────────────────►  │
     │                          │                            │
     │                          │                 9. Renderiza gráfico
     │                          │                            │


┌────────────────────────────────────────────────────────────────────────────┐
│                        FLUXO: CONTROLE DE TEMPERATURA                      │
└────────────────────────────────────────────────────────────────────────────┘

  ESP8266 (Loop Principal)
     │
     ├─── 1. Lê temperaturas (DS18B20)
     │         │
     │         ▼
     ├─── 2. Calcula erro (target - atual)
     │         │
     │         ▼
     ├─── 3. Algoritmo PID
     │         │
     │         ├─► Se temp > target + histerese → Liga Cooler
     │         │
     │         ├─► Se temp < target - histerese → Liga Heater
     │         │
     │         └─► Dentro da faixa → Desliga ambos
     │
     ├─── 4. Verifica tempo mínimo entre acionamentos
     │
     ├─── 5. Aciona relés
     │
     └─── 6. Envia heartbeat ao servidor (a cada 30s)
               │
               ▼
          POST /heartbeat
          {config_id, uptime, free_heap, temps, cooler, heater}


┌────────────────────────────────────────────────────────────────────────────┐
│                        FLUXO: AVANÇO DE ETAPA                              │
└────────────────────────────────────────────────────────────────────────────┘

  ESP8266                    Servidor                     Frontend
     │                          │                            │
     │  1. Detecta fim da etapa │                            │
     │     (tempo ou gravidade) │                            │
     │                          │                            │
     │  2. POST /stage          │                            │
     │  {config_id, newIndex}   │                            │
     │  ─────────────────────►  │                            │
     │                          │  3. UPDATE stages          │
     │                          │     SET status='completed' │
     │                          │     WHERE stage_index=old  │
     │                          │                            │
     │                          │  4. UPDATE stages          │
     │                          │     SET status='running'   │
     │                          │     WHERE stage_index=new  │
     │                          │                            │
     │                          │  5. UPDATE configurations  │
     │                          │     SET current_stage_idx  │
     │                          │                            │
     │  6. {success, new_target}│                            │
     │  ◄─────────────────────  │                            │
     │                          │                            │
     │  7. Ajusta PID para      │                            │
     │     nova temperatura     │                            │
     │                          │                            │
```

### 1.5 Tecnologias Utilizadas

| Camada | Tecnologia | Versão | Propósito |
|--------|------------|--------|-----------|
| Hardware | ESP8266 | NodeMCU | Controlador principal |
| Hardware | DS18B20 | - | Sensores de temperatura |
| Hardware | iSpindel | - | Hidrometro digital (gravidade) |
| Backend | PHP | 7.4+ | API REST |
| Backend | MySQL | 5.7+ | Banco de dados |
| Frontend | HTML5/CSS3 | - | Estrutura e estilo |
| Frontend | JavaScript | ES6+ | Lógica do cliente |
| Frontend | Tailwind CSS | CDN | Framework CSS |
| Frontend | Chart.js | - | Gráficos |
| Frontend | Font Awesome | 6.x | Ícones |

---

## 2. Estrutura de Arquivos

```
/
├── index.html              # Dashboard de monitoramento
├── config.html             # Configuração de fermentações
├── cadastro.html           # Cadastro de usuários
├── sensores.html           # Configuração de sensores
├── styles.css              # Estilos globais
├── app.js                  # JavaScript do monitor
├── config.js               # JavaScript da configuração
├── register.php            # API de cadastro
├── api.php                 # API principal unificada
│
├── config/
│   └── database.php        # Credenciais do banco
│
└── api/
    └── esp/
        ├── active.php      # Fermentação ativa
        ├── config.php      # Configuração para ESP
        ├── heartbeat.php   # Heartbeat do ESP
        ├── sensors.php     # Gerenciamento de sensores
        ├── stage.php       # Atualização de etapas
        ├── state.php       # Estado da fermentação
        └── target.php      # Notificação de alvo
```
