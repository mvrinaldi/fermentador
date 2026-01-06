# Sistema de Fermentação - Arquitetura Completa

## 🎯 Conceito Principal

**TODO o controle é feito pelo ESP8266**. O site apenas:
- Cadastra configurações e etapas
- Monitora o estado atual
- Permite pausar ou concluir fermentação

## 📊 Fluxo de Operação

### 1. Inicialização do ESP8266

```
┌─────────────────────────────────────┐
│  ESP8266 Liga                       │
└──────────┬──────────────────────────┘
           │
           ├─ Carrega estado da EEPROM
           ├─ Conecta WiFi
           ├─ Testa HTTP
           └─ Busca fermentação ativa
```

### 2. Processamento das Etapas (100% Local)

```
┌──────────────────────────────────────────────┐
│ TIPO: TEMPERATURE (tempo após atingir alvo) │
├──────────────────────────────────────────────┤
│ 1. Define temp alvo: 18°C                    │
│ 2. AGUARDA atingir 18°C ± 0.5°C              │
│ 3. Quando atingir, INICIA contagem: 7 dias   │
│ 4. Mantém temp enquanto conta                │
│ 5. Após 7 dias → próxima etapa               │
└──────────────────────────────────────────────┘

┌──────────────────────────────────────────────┐
│ TIPO: RAMP (transição gradual)              │
├──────────────────────────────────────────────┤
│ 1. Temp atual: 18°C                          │
│ 2. Temp alvo: 20°C                           │
│ 3. Tempo: 10 horas                           │
│ 4. Aumenta gradualmente 0.2°C/hora           │
│ 5. Após 10h em 20°C → próxima etapa          │
└──────────────────────────────────────────────┘

┌──────────────────────────────────────────────┐
│ TIPO: GRAVITY (espera gravidade)            │
├──────────────────────────────────────────────┤
│ 1. Define temp alvo: 20°C                    │
│ 2. Aguarda iSpindel reportar ≤ 1.012 SG      │
│ 3. Quando atingir → próxima etapa            │
│ 4. SEM TIMEOUT (aguarda indefinidamente)     │
└──────────────────────────────────────────────┘

┌──────────────────────────────────────────────┐
│ TIPO: GRAVITY_TIME (gravidade com timeout)  │
├──────────────────────────────────────────────┤
│ 1. Define temp alvo: 20°C                    │
│ 2. Aguarda gravidade ≤ 1.012 SG              │
│ 3. OU timeout de 14 dias                     │
│ 4. O QUE OCORRER PRIMEIRO → próxima etapa    │
└──────────────────────────────────────────────┘
```

### 3. Exemplo de Configuração Completa

```
CONFIGURAÇÃO: "IPA Americana"

┌───────────────────────────────────────┐
│ ETAPA 1: TEMPERATURE                  │
│ Temp: 18°C por 7 dias                 │
│ ➜ Aguarda 18°C, depois conta 7 dias   │
└───────────────────────────────────────┘
              ↓
┌───────────────────────────────────────┐
│ ETAPA 2: RAMP                         │
│ De 18°C para 20°C em 48 horas         │
│ ➜ Transição gradual (rampa)           │
└───────────────────────────────────────┘
              ↓
┌───────────────────────────────────────┐
│ ETAPA 3: TEMPERATURE                  │
│ Temp: 20°C por 3 dias                 │
│ ➜ Diacetil rest                       │
└───────────────────────────────────────┘
              ↓
┌───────────────────────────────────────┐
│ ETAPA 4: RAMP                         │
│ De 20°C para 4°C em 72 horas          │
│ ➜ Resfriamento gradual                │
└───────────────────────────────────────┘
              ↓
┌───────────────────────────────────────┐
│ ETAPA 5: TEMPERATURE                  │
│ Temp: 4°C por 3 dias                  │
│ ➜ Cold crash                          │
└───────────────────────────────────────┘
              ↓
         CONCLUÍDA! 🎉
```

## 🔄 Ciclo de Processamento

### Loop Principal (5 segundos)

```cpp
┌─────────────────────────────────────┐
│ 1. Lê sensores de temperatura      │
│    ├─ Fermentador: DS18B20         │
│    └─ Geladeira: DS18B20           │
├─────────────────────────────────────┤
│ 2. Calcula PID                      │
│    ├─ Erro = Alvo - Atual          │
│    ├─ Integral (anti-windup)       │
│    └─ Derivativo                    │
├─────────────────────────────────────┤
│ 3. Atualiza relés                   │
│    ├─ Cooler (geladeira)           │
│    └─ Heater (aquecedor)           │
└─────────────────────────────────────┘
```

### Verificação de Etapas (10 segundos)

```cpp
┌─────────────────────────────────────┐
│ 1. Calcula tempo decorrido          │
│    ├─ Horas (para rampas)           │
│    └─ Dias (para temperature)       │
├─────────────────────────────────────┤
│ 2. Processa etapa atual             │
│    ├─ TEMPERATURE: verifica tempo   │
│    ├─ RAMP: atualiza temp gradual  │
│    ├─ GRAVITY: verifica iSpindel   │
│    └─ GRAVITY_TIME: ambos          │
├─────────────────────────────────────┤
│ 3. Se concluída                     │
│    ├─ Avança índice                │
│    ├─ Reseta timer                 │
│    └─ Atualiza temp alvo           │
└─────────────────────────────────────┘
```

### Sincronização com MySQL (30 segundos)

```cpp
┌─────────────────────────────────────┐
│ 1. Busca fermentação ativa          │
│    └─ Se mudou, baixa nova config   │
├─────────────────────────────────────┤
│ 2. Verifica pause/complete          │
│    └─ Se sim, desativa local       │
├─────────────────────────────────────┤
│ 3. Envia leitura atual             │
│    ├─ Temp fermentador             │
│    ├─ Temp geladeira               │
│    ├─ Gravidade (se disponível)    │
│    └─ Temp alvo                    │
├─────────────────────────────────────┤
│ 4. Envia estado controlador        │
│    ├─ Setpoint                     │
│    ├─ Cooler ON/OFF                │
│    └─ Heater ON/OFF                │
└─────────────────────────────────────┘
```

### Envio de Estado (5 minutos)

```cpp
┌─────────────────────────────────────┐
│ Envia ao MySQL:                     │
│ ├─ Status da etapa atual            │
│ ├─ Tempo restante                   │
│ ├─ Progresso                        │
│ ├─ Temperatura atual vs alvo        │
│ └─ Gravidade (se aplicável)         │
└─────────────────────────────────────┘
```

## 🛡️ Operação Offline

### O que funciona SEM internet:

✅ **Controle de temperatura** (PID sempre ativo)
✅ **Processamento de etapas** (timer local + EEPROM)
✅ **Troca automática de etapas**
✅ **Acionamento de relés**
✅ **Persistência em EEPROM**

### O que NÃO funciona sem internet:

❌ Buscar nova configuração
❌ Enviar leituras ao MySQL
❌ Receber comandos pause/complete
❌ Atualizar site em tempo real

**IMPORTANTE**: Ao reconectar, sincroniza automaticamente!

## 🗄️ Persistência em EEPROM

```
ENDEREÇOS NA EEPROM:
├─ 64-95:   ID da fermentação ativa
├─ 96-99:   Índice da etapa atual
├─ 100-103: Timestamp de início da etapa
├─ 108:     Flag de etapa iniciada
└─ 109:     Flag de configuração salva
```

## 📱 Interação com o Site

### Site → ESP8266

```
1. Usuário cadastra configuração no site
2. Usuário clica "INICIAR"
   └─ MySQL marca config como 'active'
3. ESP8266 detecta (próxima verificação)
   └─ Baixa configuração completa
4. ESP8266 processa LOCALMENTE
```

### ESP8266 → Site

```
A cada 30 segundos:
├─ Envia leitura de sensores
├─ Envia estado dos relés
└─ Verifica comandos (pause/complete)

A cada 5 minutos:
└─ Envia estado detalhado da etapa
```

### Comandos do Site

```
PAUSAR:
1. Site atualiza status → 'paused'
2. ESP8266 detecta
3. Desativa fermentação local
4. Mantém última temp na EEPROM
5. Pode retomar depois

CONCLUIR:
1. Site atualiza status → 'completed'
2. ESP8266 detecta
3. Desativa fermentação local
4. Limpa EEPROM
```

## 🔧 Arquivos Modificados

### ESP8266

- ✅ `http_client.h` - Cliente HTTP otimizado
- ✅ `fermentacao_mysql.cpp/h` - Substitui Firebase
- ✅ `fermentacao_stages.cpp/h` - Processamento local completo
- ✅ `controle_temperatura.cpp/h` - PID independente de rede
- ✅ `network_manager.cpp/h` - HTTP ao invés de Firebase
- ✅ `gerenciador_sensores.cpp/h` - Envio via HTTP
- ✅ `main.cpp` - Loop atualizado

### Servidor PHP

- ✅ `active.php` - Fermentação ativa
- ✅ `config.php` - Configuração + status
- ✅ `reading.php` - Recebe leituras
- ✅ `control.php` - Estado do controlador
- ✅ `sensors.php` - Sensores detectados
- ✅ `target.php` - Notifica alvo atingido

## 📈 Exemplo de Logs

```
🚀 Iniciando Fermentador Inteligente - MySQL
==============================================
✅ EEPROM inicializada (512 bytes)
✅ Relés inicializados
✅ Sensores inicializados
📡 WiFi online
✅ HTTP online
🌐 Servidor Web ativo
[EEPROM] ✅ Estado restaurado: ID=123
==============================================
✅ Sistema pronto
Fermentação ativa: SIM
ID: 123
Config: IPA Americana
Etapa: 2/5
Temp alvo: 20.0°C
==============================================

[Fase] ▶️  Etapa 2/5 iniciada (tipo: RAMP)
[PID] 🌡️  Fermentador: 18.50°C
[PID] 🎯 Alvo:         18.60°C
[PID] ❄️  Cooler:       DESLIGADO
[PID] 🔥 Heater:       DESLIGADO

[Stages] 📤 Estado enviado ao MySQL
[MySQL] 📊 Leitura enviada

[Fase] ✅ Etapa 2/5 concluída
[Fase] ↪️  Indo para etapa 3/5 (20.0°C)
[Fase] ▶️  Etapa 3/5 iniciada (tipo: TEMPERATURE)
[Fase] 🎯 Temperatura alvo atingida, iniciando contagem
```

## 🎓 Resumo

| Aspecto | Responsabilidade |
|---------|------------------|
| **Controle PID** | 100% ESP8266 |
| **Etapas** | 100% ESP8266 |
| **Timers** | 100% ESP8266 |
| **Relés** | 100% ESP8266 |
| **Cadastro Config** | Site/MySQL |
| **Monitoramento** | Site/MySQL |
| **Pause/Complete** | Site → ESP |

**O ESP8266 é AUTÔNOMO e funciona offline!** ✅