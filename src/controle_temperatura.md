#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// Configuração WiFi
const char* ssid = "SUA_REDE";
const char* password = "SUA_SENHA";

// Pinos
#define PIN_SENSOR_GELADEIRA D1
#define PIN_SENSOR_FERMENTADOR D2
#define PIN_RELE_COOLER D5
#define PIN_RELE_HEATER D6
#define LED_BUILTIN D4

// Sensores DS18B20
OneWire oneWireGel(PIN_SENSOR_GELADEIRA);
OneWire oneWireFerm(PIN_SENSOR_FERMENTADOR);
DallasTemperature sensorGeladeira(&oneWireGel);
DallasTemperature sensorFermentador(&oneWireFerm);

// Servidor Web
ESP8266WebServer server(80);

// Parâmetros PID
struct PIDConfig {
  float kp = 20.0;        // Ganho proporcional
  float ki = 0.5;         // Ganho integral
  float kd = 5.0;         // Ganho derivativo
  float setpoint = 20.0;  // Temperatura alvo
  float maxIntegral = 100.0;
  float deadband = 0.3;   // Banda morta para evitar oscilação
} pid;

// Variáveis de controle
float tempGeladeira = 0;
float tempFermentador = 0;
float pidOutput = 0;
float integral = 0;
float lastError = 0;
unsigned long lastPIDTime = 0;

// Proteção de tempo mínimo entre acionamentos
unsigned long lastCoolerOn = 0;
unsigned long lastCoolerOff = 0;
unsigned long lastHeaterOn = 0;
unsigned long lastHeaterOff = 0;
const unsigned long MIN_CYCLE_TIME = 900000; // 15 minutos em ms

// Estados
bool coolerState = false;
bool heaterState = false;
bool pidEnabled = true;
bool manualMode = false;

// Logs e estatísticas
struct Statistics {
  float maxTemp = -100;
  float minTemp = 100;
  unsigned long coolerOnTime = 0;
  unsigned long heaterOnTime = 0;
  unsigned long lastCoolerStart = 0;
  unsigned long lastHeaterStart = 0;
} stats;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nSistema PID Fermentador v2.0");
  
  // Inicializa pinos
  pinMode(PIN_RELE_COOLER, OUTPUT);
  pinMode(PIN_RELE_HEATER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(PIN_RELE_COOLER, HIGH); // Relé desligado (lógica invertida)
  digitalWrite(PIN_RELE_HEATER, HIGH);
  
  // Inicializa sensores
  sensorGeladeira.begin();
  sensorFermentador.begin();
  
  // Conecta WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  
  Serial.println("\nWiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, LOW);
  
  // Carrega configuração da EEPROM
  loadConfig();
  
  // Configura servidor web
  setupWebServer();
  server.begin();
  
  Serial.println("Sistema iniciado!");
}

void loop() {
  server.handleClient();
  
  // Lê temperaturas a cada 2 segundos
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 2000) {
    lastRead = millis();
    readTemperatures();
  }
  
  // Executa PID a cada 5 segundos
  if (millis() - lastPIDTime > 5000) {
    lastPIDTime = millis();
    
    if (pidEnabled && !manualMode) {
      executePID();
    }
    
    updateStatistics();
    printStatus();
  }
}

void readTemperatures() {
  sensorGeladeira.requestTemperatures();
  sensorFermentador.requestTemperatures();
  
  float newTempGel = sensorGeladeira.getTempCByIndex(0);
  float newTempFerm = sensorFermentador.getTempCByIndex(0);
  
  // Valida leituras
  if (newTempGel != DEVICE_DISCONNECTED_C && newTempGel > -50 && newTempGel < 80) {
    tempGeladeira = newTempGel;
  }
  
  if (newTempFerm != DEVICE_DISCONNECTED_C && newTempFerm > -50 && newTempFerm < 80) {
    tempFermentador = newTempFerm;
  }
}

void executePID() {
  float error = pid.setpoint - tempFermentador;
  unsigned long now = millis();
  float dt = 5.0; // 5 segundos
  
  // Banda morta - evita oscilação pequena
  if (abs(error) < pid.deadband) {
    error = 0;
  }
  
  // Termo Proporcional
  float pTerm = pid.kp * error;
  
  // Termo Integral (com anti-windup)
  integral += error * dt;
  integral = constrain(integral, -pid.maxIntegral, pid.maxIntegral);
  float iTerm = pid.ki * integral;
  
  // Termo Derivativo
  float dTerm = pid.kd * (error - lastError) / dt;
  lastError = error;
  
  // Saída PID
  pidOutput = pTerm + iTerm + dTerm;
  pidOutput = constrain(pidOutput, -100, 100);
  
  // Aplica saída com proteção de tempo
  applyPIDOutput(now);
}

void applyPIDOutput(unsigned long now) {
  // Nunca liga cooler e heater ao mesmo tempo
  if (pidOutput > 5) {
    // Precisa resfriar
    if (canTurnOnCooler(now)) {
      turnOnCooler(now);
    }
    turnOffHeater(now);
    
  } else if (pidOutput < -5) {
    // Precisa aquecer
    if (canTurnOnHeater(now)) {
      turnOnHeater(now);
    }
    turnOffCooler(now);
    
  } else {
    // Na zona neutra - desliga tudo
    if (canTurnOffCooler(now)) {
      turnOffCooler(now);
    }
    if (canTurnOffHeater(now)) {
      turnOffHeater(now);
    }
  }
}

bool canTurnOnCooler(unsigned long now) {
  // Verifica tempo mínimo desde última operação
  return !coolerState && 
         (now - lastCoolerOff > MIN_CYCLE_TIME) &&
         (now - lastHeaterOff > 60000); // 1 min de segurança após desligar heater
}

bool canTurnOffCooler(unsigned long now) {
  // Tempo mínimo ligado: 3 minutos
  return coolerState && (now - lastCoolerOn > 180000);
}

bool canTurnOnHeater(unsigned long now) {
  return !heaterState && 
         (now - lastHeaterOff > MIN_CYCLE_TIME) &&
         (now - lastCoolerOff > 60000); // 1 min de segurança após desligar cooler
}

bool canTurnOffHeater(unsigned long now) {
  // Tempo mínimo ligado: 2 minutos
  return heaterState && (now - lastHeaterOn > 120000);
}

void turnOnCooler(unsigned long now) {
  if (!coolerState) {
    digitalWrite(PIN_RELE_COOLER, LOW); // Relé ativo em LOW
    coolerState = true;
    lastCoolerOn = now;
    stats.lastCoolerStart = now;
    Serial.println(">>> COOLER LIGADO");
  }
}

void turnOffCooler(unsigned long now) {
  if (coolerState) {
    digitalWrite(PIN_RELE_COOLER, HIGH);
    coolerState = false;
    lastCoolerOff = now;
    stats.coolerOnTime += (now - stats.lastCoolerStart);
    Serial.println(">>> COOLER DESLIGADO");
  }
}

void turnOnHeater(unsigned long now) {
  if (!heaterState) {
    digitalWrite(PIN_RELE_HEATER, LOW);
    heaterState = true;
    lastHeaterOn = now;
    stats.lastHeaterStart = now;
    Serial.println(">>> HEATER LIGADO");
  }
}

void turnOffHeater(unsigned long now) {
  if (heaterState) {
    digitalWrite(PIN_RELE_HEATER, HIGH);
    heaterState = false;
    lastHeaterOff = now;
    stats.heaterOnTime += (now - stats.lastHeaterStart);
    Serial.println(">>> HEATER DESLIGADO");
  }
}

void updateStatistics() {
  if (tempFermentador > stats.maxTemp) stats.maxTemp = tempFermentador;
  if (tempFermentador < stats.minTemp) stats.minTemp = tempFermentador;
}

void printStatus() {
  Serial.println("\n--- STATUS ---");
  Serial.printf("Fermentador: %.2f°C | Geladeira: %.2f°C\n", 
                tempFermentador, tempGeladeira);
  Serial.printf("Setpoint: %.2f°C | Erro: %.2f°C\n", 
                pid.setpoint, pid.setpoint - tempFermentador);
  Serial.printf("PID Output: %.2f | Integral: %.2f\n", pidOutput, integral);
  Serial.printf("Cooler: %s | Heater: %s\n", 
                coolerState ? "ON" : "OFF", 
                heaterState ? "ON" : "OFF");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/setpoint", HTTP_POST, handleSetpoint);
  server.on("/api/pid", HTTP_POST, handlePIDConfig);
  server.on("/api/manual", HTTP_POST, handleManual);
  server.on("/api/reset", HTTP_POST, handleReset);
}

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Controle Fermentador PID</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container { 
      max-width: 900px; 
      margin: 0 auto; 
      background: white;
      border-radius: 20px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      overflow: hidden;
    }
    .header {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      padding: 30px;
      text-align: center;
    }
    .header h1 { font-size: 28px; margin-bottom: 5px; }
    .header p { opacity: 0.9; font-size: 14px; }
    .content { padding: 30px; }
    .temp-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 20px;
      margin-bottom: 30px;
    }
    .temp-card {
      background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
      padding: 25px;
      border-radius: 15px;
      text-align: center;
      box-shadow: 0 5px 15px rgba(0,0,0,0.1);
    }
    .temp-card h3 {
      font-size: 14px;
      color: #666;
      margin-bottom: 10px;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .temp-value {
      font-size: 42px;
      font-weight: bold;
      color: #333;
      margin: 10px 0;
    }
    .temp-unit { font-size: 24px; color: #999; }
    .status-indicator {
      display: inline-block;
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-left: 8px;
      animation: pulse 2s infinite;
    }
    .status-on { background: #4caf50; box-shadow: 0 0 10px #4caf50; }
    .status-off { background: #ccc; }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    .controls {
      background: #f8f9fa;
      padding: 25px;
      border-radius: 15px;
      margin-bottom: 20px;
    }
    .control-group {
      margin-bottom: 20px;
    }
    .control-group label {
      display: block;
      font-weight: 600;
      margin-bottom: 8px;
      color: #333;
    }
    .input-group {
      display: flex;
      gap: 10px;
    }
    input[type="number"] {
      flex: 1;
      padding: 12px;
      border: 2px solid #ddd;
      border-radius: 8px;
      font-size: 16px;
      transition: border-color 0.3s;
    }
    input[type="number"]:focus {
      outline: none;
      border-color: #667eea;
    }
    .btn {
      padding: 12px 24px;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .btn-primary {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
    }
    .btn-primary:hover {
      transform: translateY(-2px);
      box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
    }
    .btn-danger {
      background: #f44336;
      color: white;
    }
    .btn-danger:hover {
      background: #d32f2f;
    }
    .pid-values {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 15px;
    }
    .info-box {
      background: #e3f2fd;
      padding: 15px;
      border-radius: 10px;
      margin-top: 20px;
      border-left: 4px solid #2196f3;
    }
    .info-box p {
      color: #1976d2;
      font-size: 14px;
      line-height: 1.6;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🍺 Controle de Fermentação PID</h1>
      <p>Sistema de controle preciso de temperatura</p>
    </div>
    
    <div class="content">
      <div class="temp-grid">
        <div class="temp-card">
          <h3>🎯 Fermentador</h3>
          <div class="temp-value">
            <span id="tempFerm">--</span>
            <span class="temp-unit">°C</span>
          </div>
          <div>Alvo: <strong id="setpoint">--</strong>°C</div>
        </div>
        
        <div class="temp-card">
          <h3>❄️ Geladeira</h3>
          <div class="temp-value">
            <span id="tempGel">--</span>
            <span class="temp-unit">°C</span>
          </div>
        </div>
        
        <div class="temp-card">
          <h3>⚡ Status</h3>
          <div style="margin: 15px 0;">
            <div style="margin: 10px 0;">
              Cooler <span id="coolerStatus" class="status-indicator status-off"></span>
            </div>
            <div>
              Heater <span id="heaterStatus" class="status-indicator status-off"></span>
            </div>
          </div>
        </div>
      </div>
      
      <div class="controls">
        <div class="control-group">
          <label>🎯 Temperatura Alvo</label>
          <div class="input-group">
            <input type="number" id="newSetpoint" value="20" step="0.5" min="0" max="40">
            <button class="btn btn-primary" onclick="setSetpoint()">Aplicar</button>
          </div>
        </div>
        
        <div class="control-group">
          <label>⚙️ Parâmetros PID</label>
          <div class="pid-values">
            <input type="number" id="kp" placeholder="Kp" step="0.1" value="20">
            <input type="number" id="ki" placeholder="Ki" step="0.1" value="0.5">
            <input type="number" id="kd" placeholder="Kd" step="0.1" value="5">
          </div>
          <button class="btn btn-primary" style="margin-top: 10px; width: 100%;" onclick="setPID()">
            Atualizar PID
          </button>
        </div>
        
        <div class="control-group">
          <button class="btn btn-danger" style="width: 100%;" onclick="resetSystem()">
            🔄 Resetar Sistema
          </button>
        </div>
      </div>
      
      <div class="info-box">
        <p><strong>ℹ️ Proteção de Ciclo:</strong> O sistema possui um delay de 15 minutos entre acionamentos do cooler/heater para proteger o compressor. Cooler permanece ligado por no mínimo 3 minutos, heater por 2 minutos.</p>
      </div>
    </div>
  </div>

  <script>
    function updateStatus() {
      fetch('/api/status')
        .then(r => r.json())
        .then(data => {
          document.getElementById('tempFerm').textContent = data.tempFermentador.toFixed(1);
          document.getElementById('tempGel').textContent = data.tempGeladeira.toFixed(1);
          document.getElementById('setpoint').textContent = data.setpoint.toFixed(1);
          
          document.getElementById('coolerStatus').className = 
            'status-indicator ' + (data.coolerOn ? 'status-on' : 'status-off');
          document.getElementById('heaterStatus').className = 
            'status-indicator ' + (data.heaterOn ? 'status-on' : 'status-off');
        });
    }
    
    function setSetpoint() {
      const value = document.getElementById('newSetpoint').value;
      fetch('/api/setpoint', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'value=' + value
      }).then(() => alert('Setpoint atualizado!'));
    }
    
    function setPID() {
      const kp = document.getElementById('kp').value;
      const ki = document.getElementById('ki').value;
      const kd = document.getElementById('kd').value;
      fetch('/api/pid', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: `kp=${kp}&ki=${ki}&kd=${kd}`
      }).then(() => alert('PID atualizado!'));
    }
    
    function resetSystem() {
      if (confirm('Resetar o sistema e limpar integral?')) {
        fetch('/api/reset', {method: 'POST'})
          .then(() => alert('Sistema resetado!'));
      }
    }
    
    updateStatus();
    setInterval(updateStatus, 3000);
  </script>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"tempFermentador\":" + String(tempFermentador, 2) + ",";
  json += "\"tempGeladeira\":" + String(tempGeladeira, 2) + ",";
  json += "\"setpoint\":" + String(pid.setpoint, 2) + ",";
  json += "\"pidOutput\":" + String(pidOutput, 2) + ",";
  json += "\"coolerOn\":" + String(coolerState ? "true" : "false") + ",";
  json += "\"heaterOn\":" + String(heaterState ? "true" : "false") + ",";
  json += "\"pidEnabled\":" + String(pidEnabled ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSetpoint() {
  if (server.hasArg("value")) {
    pid.setpoint = server.arg("value").toFloat();
    saveConfig();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}

void handlePIDConfig() {
  if (server.hasArg("kp")) pid.kp = server.arg("kp").toFloat();
  if (server.hasArg("ki")) pid.ki = server.arg("ki").toFloat();
  if (server.hasArg("kd")) pid.kd = server.arg("kd").toFloat();
  
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleManual() {
  // Implementar modo manual se necessário
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  integral = 0;
  lastError = 0;
  stats.maxTemp = -100;
  stats.minTemp = 100;
  stats.coolerOnTime = 0;
  stats.heaterOnTime = 0;
  
  server.send(200, "text/plain", "OK");
}

void saveConfig() {
  EEPROM.begin(512);
  EEPROM.put(0, pid);
  EEPROM.commit();
  Serial.println("Configuração salva!");
}

void loadConfig() {
  EEPROM.begin(512);
  PIDConfig loadedPid;
  EEPROM.get(0, loadedPid);
  
  // Valida valores carregados
  if (loadedPid.kp > 0 && loadedPid.kp < 1000 &&
      loadedPid.ki >= 0 && loadedPid.ki < 100 &&
      loadedPid.kd >= 0 && loadedPid.kd < 100 &&
      loadedPid.setpoint > 0 && loadedPid.setpoint < 50) {
    pid = loadedPid;
    Serial.println("Configuração carregada da EEPROM");
  } else {
    Serial.println("Usando configuração padrão");
  }
}