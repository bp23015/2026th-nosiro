/*
  Motor_Test.ino
  ESP32 + TB6643KQ×2 のモーター単体テスト用スケッチ。
  スマホ/PCのブラウザからWi-Fi経由でモーターをON/OFF・正逆転・速度制御する。

  motor_esp.py（MicroPython版）のArduino移植版だが、PWMの掛け方は
  merge_v1.ino（Pid関数）の実績のある方式に合わせてある：
    analogWrite(IN1, speed); analogWrite(IN2, 0);
  つまり「片方のIN固定LOW + もう片方PWM」で、PWMは
  「通常動作 ⇔ ストップ(Hi-Z)」を繰り返す（データシート推奨の
  「片方H固定+もう片方PWM＝通常動作⇔ショートブレーキ」ではない点に注意）。

  必要ライブラリ（Arduino IDEのライブラリマネージャからインストール）：
    - ESPAsyncWebServer (ESP32Async 版)
    - AsyncTCP (ESP32Async 版)
    - ArduinoJson (v7系)
*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// アクセスポイントの設定（ssidとpwは自由に決めていい）
const char* ssid = "ESP32_MotorTest2";
const char* password = "esp32wroom";

// モーターのIN1/IN2ピン（merge_v1.inoと同じ配線）
const int RM_IN1 = 13;  // 右モーター
const int RM_IN2 = 14;
const int LM_IN1 = 32;  // 左モーター
const int LM_IN2 = 33;

// モーター番号(1=右, 2=左)でひけるようにしておく。添字0は未使用
const int motor_in1[3] = { 0, RM_IN1, LM_IN1 };
const int motor_in2[3] = { 0, RM_IN2, LM_IN2 };

// 各モーターの状態
bool motor_on[3] = { false, false, false };
int motor_dir[3] = { 0, 0, 0 };        // 0=Forward, 1=Reverse
int max_speed[3] = { 255, 255, 255 };  // 0〜255（analogWriteの分解能に合わせる）

// 配線の都合で正転/逆転が実際の回転方向と逆になっている場合はtrueにする
// （モーターごとに独立。実機でForward/Reverseボタンを押して確認し調整する）
bool invert_dir[3] = { false, false, false };

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>ESP32 Motor Test</title>
<style>
  body {
    font-family: sans-serif;
    margin: 0;
    padding: 16px;
    max-width: 480px;
    margin-left: auto;
    margin-right: auto;
    background: #f2f2f2;
    color: #222;
  }
  h1 {
    font-size: 1.3em;
    text-align: center;
    margin: 8px 0 20px;
  }
  .card {
    background: #fff;
    border-radius: 12px;
    padding: 16px;
    margin-bottom: 16px;
    box-shadow: 0 1px 4px rgba(0,0,0,0.15);
  }
  .buttons {
    display: flex;
    gap: 12px;
  }
  button {
    flex: 1;
    padding: 16px;
    font-size: 1.1em;
    border: 2px solid #d5d5d5;
    border-radius: 10px;
    background: #fff;
    color: #444;
    -webkit-tap-highlight-color: transparent;
  }
  button:active {
    background: #f0f0f0;
  }
  button.active {
    background: #2d7ff9;
    color: #fff;
    border-color: #2d7ff9;
  }
  button.active:active {
    background: #1a5fd0;
  }
  input[type="range"] {
    width: 100%;
    height: 32px;
    margin: 12px 0 4px;
  }
  .row {
    display: flex;
    justify-content: space-between;
    font-size: 0.95em;
    color: #444;
  }
  #status {
    font-weight: bold;
  }
</style>
</head>
<body>
<h1>ESP32 Motor Test (TB6643KQ)</h1>

<div class="card">
  <div class="row"><strong>All Motors</strong></div>
  <div class="buttons" style="margin-top: 8px;">
    <button type="button" onclick="allRun(true)">ALL ON</button>
    <button type="button" onclick="allRun(false)">ALL OFF</button>
  </div>
</div>

<div class="card">
  <div class="row"><strong>Right Motor</strong></div>
  <div class="buttons" style="margin-top: 8px;">
    <button type="button" id="fwd1" class="active" onclick="setDir(1, 0)">Forward</button>
    <button type="button" id="rev1" onclick="setDir(1, 1)">Reverse</button>
  </div>
  <div class="buttons" style="margin-top: 8px;">
    <button type="button" id="on1" onclick="setRun(1, true)">ON</button>
    <button type="button" id="off1" class="active" onclick="setRun(1, false)">OFF</button>
  </div>
  <div class="row" style="margin-top: 16px;">
    <span>Speed</span>
    <span><span id="speedVal1">255</span> / 255</span>
  </div>
  <input type="range" id="speed1" min="0" max="255" value="255"
         oninput="onSlide(1, this.value)" />
</div>

<div class="card">
  <div class="row"><strong>Left Motor</strong></div>
  <div class="buttons" style="margin-top: 8px;">
    <button type="button" id="fwd2" class="active" onclick="setDir(2, 0)">Forward</button>
    <button type="button" id="rev2" onclick="setDir(2, 1)">Reverse</button>
  </div>
  <div class="buttons" style="margin-top: 8px;">
    <button type="button" id="on2" onclick="setRun(2, true)">ON</button>
    <button type="button" id="off2" class="active" onclick="setRun(2, false)">OFF</button>
  </div>
  <div class="row" style="margin-top: 16px;">
    <span>Speed</span>
    <span><span id="speedVal2">255</span> / 255</span>
  </div>
  <input type="range" id="speed2" min="0" max="255" value="255"
         oninput="onSlide(2, this.value)" />
</div>

<div class="card">
  <div class="row">
    <span>Connection</span>
    <span id="status">connecting...</span>
  </div>
</div>

<script>
let ws;

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');

  ws.onopen = () => {
    document.getElementById('status').innerText = 'connected';
  };

  ws.onclose = () => {
    document.getElementById('status').innerText = 'disconnected (retrying...)';
    setTimeout(connect, 1000);
  };

  ws.onerror = () => {
    ws.close();
  };
}

function sendCmd(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(obj));
  }
}

function onSlide(motor, value) {
  document.getElementById('speedVal' + motor).innerText = value;
  sendCmd({ cmd: 'set', motor: motor, value: parseInt(value, 10) });
}

function setDir(motor, dir) {
  document.getElementById('fwd' + motor).classList.toggle('active', dir === 0);
  document.getElementById('rev' + motor).classList.toggle('active', dir === 1);
  sendCmd({ cmd: 'dir', motor: motor, dir: dir });
}

function setRun(motor, on) {
  document.getElementById('on' + motor).classList.toggle('active', on);
  document.getElementById('off' + motor).classList.toggle('active', !on);
  sendCmd({ cmd: on ? 'on' : 'off', motor: motor });
}

function allRun(on) {
  setRun(1, on);
  setRun(2, on);
}

connect();
</script>
</body>
</html>
)rawliteral";

// IN1/IN2への出力を現在の状態(on/off・方向・速度)から計算して反映する
void apply_motor_state(int motor) {
  if (!motor_on[motor]) {
    analogWrite(motor_in1[motor], 0);
    analogWrite(motor_in2[motor], 0);
    return;
  }

  int dir = motor_dir[motor];
  if (invert_dir[motor]) dir = 1 - dir;

  int speed = max_speed[motor];
  if (dir == 0) {
    analogWrite(motor_in1[motor], speed);
    analogWrite(motor_in2[motor], 0);
  } else {
    analogWrite(motor_in1[motor], 0);
    analogWrite(motor_in2[motor], speed);
  }
}

// 両モーターを停止し、方向・速度も初期値（Forward・フル速度）に戻す。
// HTML側の初期表示（Forwardボタン active・スライダー255）と状態を一致させ、
// 前回接続時のon/off・方向・速度が新しい接続に持ち越されないようにする
void reset_motors() {
  for (int motor = 1; motor <= 2; motor++) {
    motor_on[motor] = false;
    motor_dir[motor] = 0;
    max_speed[motor] = 255;
    apply_motor_state(motor);
  }
}

// ブラウザから受け取ったJSONコマンドを実行する
void handle_command(JsonDocument& doc) {
  int motor = doc["motor"] | 0;
  if (motor != 1 && motor != 2) return;

  const char* cmd = doc["cmd"] | "";

  if (strcmp(cmd, "on") == 0) {
    motor_on[motor] = true;
    apply_motor_state(motor);
  } else if (strcmp(cmd, "off") == 0) {
    motor_on[motor] = false;
    apply_motor_state(motor);
  } else if (strcmp(cmd, "dir") == 0) {
    int d = doc["dir"] | -1;
    if (d != 0 && d != 1) return;
    motor_dir[motor] = d;
    if (motor_on[motor]) apply_motor_state(motor);
  } else if (strcmp(cmd, "set") == 0) {
    int v = doc["value"] | max_speed[motor];
    v = constrain(v, 0, 255);
    max_speed[motor] = v;
    if (motor_on[motor]) apply_motor_state(motor);
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS client #%u connected\n", client->id());
    // 新しい接続が確立した時点で、前回接続時の状態を持ち越さないようリセットする
    reset_motors();
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS client #%u disconnected\n", client->id());
    // 接続が切れた場合も安全のため両モーターを停止する
    reset_motors();
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (!err) {
        handle_command(doc);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RM_IN1, OUTPUT);
  pinMode(RM_IN2, OUTPUT);
  pinMode(LM_IN1, OUTPUT);
  pinMode(LM_IN2, OUTPUT);
  apply_motor_state(1);
  apply_motor_state(2);

  WiFi.softAP(ssid, password);
  Serial.print("Hosting on ");
  Serial.println(WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();  // 切断済みクライアントの後片付け（メモリリーク防止）
}
