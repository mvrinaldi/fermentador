# Sistema de Alertas - Fermentação

Sistema completo de alertas para monitoramento de fermentação com notificações via **WhatsApp** e **Telegram**.

## 📦 Arquivos

```
alertas/
├── AlertSystem.php           # Classe principal do sistema
├── alertas.html              # Interface de configuração
├── api/
│   ├── alerts.php            # Endpoints REST da API
│   └── AlertIntegration.php  # Hooks para integração
├── sql/
│   └── install_alerts.sql    # Script de instalação do banco
└── README.md                 # Este arquivo
```

## 🚀 Instalação

### 1. Banco de Dados

Execute o script SQL no phpMyAdmin:

```sql
-- Acesse phpMyAdmin e execute o conteúdo de:
sql/install_alerts.sql
```

### 2. Arquivos PHP

Copie os arquivos para seu servidor:

```
seu-servidor/
├── AlertSystem.php           # Raiz ou pasta api/
├── alertas.html              # Raiz (acessível pelo navegador)
├── api/
│   ├── alerts.php            # Nova API de alertas
│   └── AlertIntegration.php  # Integração
```

### 3. Integrar com api.php Existente

Adicione no início do seu `api.php`:

```php
require_once __DIR__ . '/AlertSystem.php';
require_once __DIR__ . '/api/AlertIntegration.php';
```

Após o handler de **heartbeat**, adicione:

```php
// Verificar alertas a cada heartbeat
if ($configId) {
    AlertIntegration::checkAlertsOnHeartbeat($pdo, $configId);
}
```

No handler de **/stage** (avanço de etapa):

```php
// Após atualizar etapa no banco
AlertIntegration::onStageCompleted($pdo, $configId, $previousIndex, $stageName, $nextStageName);

// Se última etapa
if ($isLastStage) {
    AlertIntegration::onFermentationCompleted($pdo, $configId, $configName);
}
```

## ⚙️ Configuração

### Acessar Interface

Abra `https://seu-servidor.com/alertas.html`

### Configurar WhatsApp (CallMeBot - Gratuito)

1. Adicione **+34 644 71 81 99** aos contatos
2. Envie via WhatsApp: `I allow callmebot to send me messages`
3. Copie a API Key recebida
4. Cole na interface de configuração

### Configurar Telegram (Gratuito e Ilimitado)

1. Fale com **@BotFather** no Telegram
2. Crie um bot com `/newbot`
3. Copie o Token
4. Inicie conversa com seu bot
5. Acesse: `https://api.telegram.org/botSEU_TOKEN/getUpdates`
6. Copie o Chat ID da resposta

## 📡 API Endpoints

| Método | Endpoint | Descrição |
|--------|----------|-----------|
| GET | `/api/alerts.php?action=` | Lista alertas não lidos |
| GET | `/api/alerts.php?action=all` | Lista todos alertas |
| GET | `/api/alerts.php?action=check` | Executa verificação |
| POST | `/api/alerts.php?action=read` | Marca como lido |
| POST | `/api/alerts.php?action=read-all` | Marca todos como lidos |
| GET | `/api/alerts.php?action=config` | Retorna configurações |
| POST | `/api/alerts.php?action=config` | Salva configurações |
| POST | `/api/alerts.php?action=test` | Envia teste |
| GET | `/api/alerts.php?action=stats` | Estatísticas |

## 🔔 Tipos de Alerta

| Tipo | Nível | Descrição |
|------|-------|-----------|
| Temperatura fora do range | ⚠️ Warning | Diferença > 2°C do alvo |
| Temperatura crítica | 🔴 Critical | Diferença > 4°C do alvo |
| ESP Offline | 🔴 Critical | Sem heartbeat > 5 minutos |
| iSpindel sem dados | ⚠️ Warning | Sem leitura > 2 horas |
| Bateria baixa | ⚠️ Warning | < 3.5V |
| Bateria crítica | 🔴 Critical | < 3.2V |
| Memória ESP baixa | ⚠️ Warning | < 30KB |
| Memória ESP crítica | 🔴 Critical | < 15KB |
| Etapa concluída | ℹ️ Info | Etapa avançou |
| Fermentação concluída | ℹ️ Info | Última etapa terminou |
| Gravidade atingida | ℹ️ Info | Gravidade alvo alcançada |

## ⏰ Cooldown (Anti-Spam)

Alertas do mesmo tipo não são repetidos dentro do período de cooldown (padrão: 30 minutos).

## 🔧 Configurações Disponíveis

| Configuração | Padrão | Descrição |
|--------------|--------|-----------|
| `temp_tolerance` | 2°C | Diferença para aviso |
| `temp_critical_tolerance` | 4°C | Diferença para crítico |
| `esp_offline_seconds` | 300s | Tempo sem heartbeat |
| `cooldown_minutes` | 30min | Não repetir alerta |
| `min_level` | warning | Nível mínimo para notificar |

## 📱 Exemplos de Mensagens

```
🚨 CRÍTICO: Temperatura ACIMA do alvo! Atual: 24.5°C | Alvo: 18.0°C | Diferença: 6.5°C

⚠️ Aviso: Temperatura acima do alvo. Atual: 20.2°C | Alvo: 18.0°C

🚨 CRÍTICO: ESP OFFLINE há 15 minutos! Controle de temperatura PARADO.

✅ Etapa 1 concluída: Fermentação Principal | Próxima: Diacetyl Rest

🎉 FERMENTAÇÃO CONCLUÍDA: IPA 2026! Hora de engarrafar!

🎯 Gravidade alvo atingida! Atual: 1.010 | Alvo: 1.012
```

## 🧪 Testando

1. Acesse `alertas.html`
2. Configure WhatsApp ou Telegram
3. Clique em "Enviar Teste"
4. Verifique se recebeu a mensagem no celular

## 📝 Logs

Erros e envios são logados via `error_log()`. Verifique:
- Hostinger: Arquivos → Logs → error_log
- Local: `/var/log/apache2/error.log`

## ❓ Troubleshooting

### WhatsApp não envia
- Verifique se enviou a mensagem de autorização para o CallMeBot
- Confirme número no formato correto (5541999999999)
- CallMeBot tem limite de mensagens por dia

### Telegram não envia
- Confirme que iniciou conversa com o bot
- Verifique se o Chat ID está correto
- Teste a URL da API no navegador

### Alertas não são criados
- Verifique se há fermentação ativa
- Confirme que a integração foi adicionada ao api.php
- Verifique logs de erro do PHP

## 📄 Licença

Uso livre para projetos pessoais de cerveja artesanal 🍺
