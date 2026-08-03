## htmlをWebSocketでリアルタイム表示し、on/offとスライドバーに応じてモーターの回転速度を操作する
## ESP32 WROOM 32E版（Raspberry Pi Pico 2W用 motor.py からの移植）
## 変更点:
##   - 内蔵温度センサー機能は削除（ESP32には同等のセンサーがないため）
##   - モータードライバをTB67H450×2からTB6643KQ×2に変更
##     （TB6643KQは専用PWMピンを持たず、IN1/IN2の2入力のみで
##      正転/逆転/ショートブレーキ/ストップを選択する方式のため、
##      モーター制御ロジックを全面的に書き換え）
##   - PWM/入力ピンをESP32のGPIOに再割当て
##   - APモードのauthmodeを明示指定

import network
import socket
import machine
from machine import Pin, PWM
import utime

utime.sleep_ms(3000) #待機

try:
    import hashlib
except ImportError:
    import uhashlib as hashlib

try:
    import binascii
except ImportError:
    import ubinascii as binascii

try:
    import ujson as json
except ImportError:
    import json

# PWMセットアップ（モーター2個、ドライバはTB6643KQ）
# TB6643KQは専用のPWM入力ピンを持たず、IN1・IN2の2本の入力だけで
# 正転/逆転/ショートブレーキ/ストップの4モードを選択する方式:
#   IN1  IN2  モード
#   H    H    ショートブレーキ
#   L    H    正転/逆転（どちらかは配線依存）
#   H    L    逆転/正転
#   L    L    ストップ（ハイインピーダンス）
# 速度はIN1・IN2のどちらかをPWM駆動して「ショートブレーキ⇔通常動作」を
# 繰り返す時間比率で決める（IC内部でデッドタイムを生成するため外部処理は不要）。
# そのためIN1・IN2の両方をPWMオブジェクトとして扱い、片方をduty_u16(65535)で
# 常時Hに固定、もう片方をduty_u16(65535 - speed)でPWM駆動する。
# ESP32のブート関連ピン（0, 2, 12, 15等）や入力専用ピン（34〜39）を避けて選定
motor1_in1 = PWM(Pin(13, Pin.OUT))
motor1_in2 = PWM(Pin(14, Pin.OUT))
motor2_in1 = PWM(Pin(32, Pin.OUT))
motor2_in2 = PWM(Pin(33, Pin.OUT))
for _pin in (motor1_in1, motor1_in2, motor2_in1, motor2_in2):
    _pin.freq(2000)
    _pin.duty_u16(0)  # 起動時はストップ（L, L）

# アクセスポイントの設定(ssidとpwは自由に決めていい)
ssid = 'ESP32_AP'
pw = 'esp32wroom'

# IN1/IN2のPWMオブジェクトをモーター番号でひけるようにしておく
motor_ins = {1: (motor1_in1, motor1_in2), 2: (motor2_in1, motor2_in2)}

# 各モーターの現在の回転方向（0=正転 / 1=逆転）
motor_dir = {1: 0, 2: 0}

# スライドバーで設定する各モーターの最大デューティ値（0〜65535＝速度）
# onコマンドはこの値を上限として使う（モーターごとに独立）
max_speed = {1: 65535, 2: 65535}

# 各モーターが「on」状態かどうか。onのときだけスライドバーの変更を即座に反映する
motor_on = {1: False, 2: False}

# WebSocketハンドシェイクで使う固定のマジック文字列（RFC 6455）
WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


# アクセスポイント関連
def host(ssid, pw):
    ap = network.WLAN(network.AP_IF)
    # ESP32はauthmodeを明示しないとオープンAPになる場合があるため指定する
    ap.config(essid=ssid, password=pw, authmode=network.AUTH_WPA2_PSK)
    ap.active(True)

    while not ap.active():
        pass

    ip = ap.ifconfig()[0]
    print(f'Hosting on {ip}')
    return ip

# ソケットを開く
def open_socket(ip):
    address = socket.getaddrinfo('0.0.0.0', 80)[0][-1]
    connection = socket.socket()
    connection.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    connection.bind(address)
    connection.listen(5)
    return connection

# 最初の1回だけ返すHTML。以降のモーター操作はWebSocket経由で行う
def build_html():
    html = f"""
            <!DOCTYPE html>
            <html>
            <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
            <title>ESP32 Motor Control</title>
            <style>
                body {{
                    font-family: sans-serif;
                    margin: 0;
                    padding: 16px;
                    max-width: 480px;
                    margin-left: auto;
                    margin-right: auto;
                    background: #f2f2f2;
                    color: #222;
                }}
                h1 {{
                    font-size: 1.3em;
                    text-align: center;
                    margin: 8px 0 20px;
                }}
                .card {{
                    background: #fff;
                    border-radius: 12px;
                    padding: 16px;
                    margin-bottom: 16px;
                    box-shadow: 0 1px 4px rgba(0,0,0,0.15);
                }}
                .buttons {{
                    display: flex;
                    gap: 12px;
                }}
                button {{
                    flex: 1;
                    padding: 16px;
                    font-size: 1.1em;
                    border: 2px solid #d5d5d5;
                    border-radius: 10px;
                    background: #fff;
                    color: #444;
                    -webkit-tap-highlight-color: transparent;
                }}
                button:active {{
                    background: #f0f0f0;
                }}
                button.active {{
                    background: #2d7ff9;
                    color: #fff;
                    border-color: #2d7ff9;
                }}
                button.active:active {{
                    background: #1a5fd0;
                }}
                input[type="range"] {{
                    width: 100%;
                    height: 32px;
                    margin: 12px 0 4px;
                }}
                .row {{
                    display: flex;
                    justify-content: space-between;
                    font-size: 0.95em;
                    color: #444;
                }}
                #status {{
                    font-weight: bold;
                }}
            </style>
            </head>
            <body>
            <h1>ESP32 Motor Control</h1>

            <div class="card">
                <div class="row"><strong>All Motors</strong></div>
                <div class="buttons" style="margin-top: 8px;">
                    <button type="button" onclick="allRun(true)">ALL ON</button>
                    <button type="button" onclick="allRun(false)">ALL OFF</button>
                </div>
            </div>

            <div class="card">
                <div class="row"><strong>Motor 1</strong></div>
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
                    <span><span id="speedVal1">{max_speed[1]}</span> / 65535</span>
                </div>
                <input type="range" id="speed1" min="0" max="65535" value="{max_speed[1]}"
                       oninput="onSlide(1, this.value)" />
            </div>

            <div class="card">
                <div class="row"><strong>Motor 2</strong></div>
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
                    <span><span id="speedVal2">{max_speed[2]}</span> / 65535</span>
                </div>
                <input type="range" id="speed2" min="0" max="65535" value="{max_speed[2]}"
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

            function connect() {{
                ws = new WebSocket('ws://' + location.host + '/ws');

                ws.onopen = () => {{
                    document.getElementById('status').innerText = 'connected';
                }};

                ws.onclose = () => {{
                    document.getElementById('status').innerText = 'disconnected (retrying...)';
                    // ESP32の再起動やWi-Fi切断に備えて自動的に再接続を試みる
                    setTimeout(connect, 1000);
                }};

                ws.onerror = () => {{
                    ws.close();
                }};
            }}

            function sendCmd(obj) {{
                if (ws && ws.readyState === WebSocket.OPEN) {{
                    ws.send(JSON.stringify(obj));
                }}
            }}

            // スライダーはドラッグ中ずっと動くので、接続が生きている限り
            // 応答を待たずに毎回そのまま送る（WebSocketは接続張りっぱなしなので
            // HTTPの都度接続より十分速い）
            function onSlide(motor, value) {{
                document.getElementById('speedVal' + motor).innerText = value;
                sendCmd({{cmd: 'set', motor: motor, value: parseInt(value, 10)}});
            }}

            // Forward/Reverseボタン：押されたほうをactiveにして色を変え、もう片方は戻す
            function setDir(motor, dir) {{
                document.getElementById('fwd' + motor).classList.toggle('active', dir === 0);
                document.getElementById('rev' + motor).classList.toggle('active', dir === 1);
                sendCmd({{cmd: 'dir', motor: motor, dir: dir}});
            }}

            // ON/OFFボタン：現在の状態にあわせて色を変える
            function setRun(motor, on) {{
                document.getElementById('on' + motor).classList.toggle('active', on);
                document.getElementById('off' + motor).classList.toggle('active', !on);
                sendCmd({{cmd: on ? 'on' : 'off', motor: motor}});
            }}

            // ALL ON / ALL OFFボタン：モーター1と2の両方に同じ操作を行う
            function allRun(on) {{
                setRun(1, on);
                setRun(2, on);
            }}

            connect();
            </script>
            </body>
            </html>
            """
    return html

# client.send()は渡したデータを必ず全部送るとは限らない（内部バッファの都合で
# 一部だけ送って返ってくることがある）ため、送りきるまでループするヘルパー
def sendall(client, data):
    if isinstance(data, str):
        data = data.encode()
    mv = memoryview(data)
    total = 0
    while total < len(mv):
        try:
            sent = client.send(mv[total:])
        except OSError:
            break
        if not sent:
            break
        total += sent

# HTTPレスポンスを組み立てて送信する（通常ページ用）
def send_response(client, body, content_type):
    header = (
        "HTTP/1.1 200 OK\r\n"
        f"Content-Type: {content_type}\r\n"
        "Connection: close\r\n"
        "\r\n"
    )
    sendall(client, header)
    sendall(client, body)

# リクエストのヘッダー部分を辞書にパースする（キーは小文字化）
def parse_headers(header_lines):
    headers = {}
    for line in header_lines:
        if ':' in line:
            key, value = line.split(':', 1)
            headers[key.strip().lower()] = value.strip()
    return headers

# WebSocketハンドシェイクのSec-WebSocket-Acceptを計算する
def compute_accept(key):
    digest = hashlib.sha1((key + WS_MAGIC).encode()).digest()
    return binascii.b2a_base64(digest).decode().strip()

# WebSocketへのアップグレード応答を送る
def do_ws_handshake(client, headers):
    key = headers.get('sec-websocket-key')
    if not key:
        return False
    accept = compute_accept(key)
    response = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept}\r\n"
        "\r\n"
    )
    sendall(client, response)
    return True

# クライアントから届いたWebSocketフレームを1枚デコードする（クライアント→サーバーは必ずマスクされる）
def decode_ws_frame(data):
    if len(data) < 2:
        return None
    b0 = data[0]
    b1 = data[1]
    opcode = b0 & 0x0F
    masked = (b1 & 0x80) != 0
    length = b1 & 0x7F
    idx = 2

    if length == 126:
        if len(data) < idx + 2:
            return None
        length = (data[idx] << 8) | data[idx + 1]
        idx += 2
    elif length == 127:
        if len(data) < idx + 8:
            return None
        length = int.from_bytes(data[idx:idx + 8], 'big')
        idx += 8

    if masked:
        if len(data) < idx + 4:
            return None
        mask = data[idx:idx + 4]
        idx += 4
        raw = data[idx:idx + length]
        payload = bytearray(raw)
        for k in range(len(payload)):
            payload[k] ^= mask[k % 4]
        payload = bytes(payload)
    else:
        payload = data[idx:idx + length]

    return opcode, payload

# 配線の都合で正転/逆転が実際のモーターの回転方向と逆になっている場合はTrueにする
# （UI側のForward/Reverseボタンの意味はそのままに、IN1/IN2どちらを
#   固定H・どちらをPWM駆動するかの割り当てだけを入れ替える）
INVERT_DIRECTION = True

# UIで選ばれた論理的な方向（0=Forward, 1=Reverse）を、実際にIN1/IN2へ
# 割り当てる物理的な方向（0または1）に変換する
def physical_dir(logical_dir):
    if INVERT_DIRECTION:
        return 1 - logical_dir
    return logical_dir

# TB6643KQはIN1/IN2の2入力のみで正転/逆転/ショートブレーキ/ストップを
# 選択する（専用PWMピンがない）。速度制御はIN1かIN2のどちらかを
# PWM駆動して「通常動作⇔ショートブレーキ」の時間比率を変えることで行う
# （データシート記載の方式。デッドタイムはIC内部で自動生成される）。
#   phys_dir == 0: IN2を常時H固定、IN1をPWM駆動 → 真理値表の L,H（通常）/H,H（ブレーキ）を往復
#   phys_dir == 1: IN1を常時H固定、IN2をPWM駆動 → 真理値表の H,L（通常）/H,H（ブレーキ）を往復
# PWM側のduty（Hの時間比率）は「1 - 速度」。デューティが低い（Hの時間が短い）
# ほど通常動作の時間が長くなり速く回る。
def apply_motor_state(motor):
    in1, in2 = motor_ins[motor]

    if not motor_on[motor]:
        # IN1=L, IN2=L で真のストップ（ハイインピーダンス）にする
        in1.duty_u16(0)
        in2.duty_u16(0)
        return

    speed = max_speed[motor]
    pwm_duty = 65535 - speed
    if physical_dir(motor_dir[motor]) == 0:
        in2.duty_u16(65535)
        in1.duty_u16(pwm_duty)
    else:
        in1.duty_u16(65535)
        in2.duty_u16(pwm_duty)

# クライアントから受け取ったJSONコマンドを実行する
def handle_command(msg):
    try:
        obj = json.loads(msg)
    except ValueError:
        return

    # どのモーター宛のコマンドか（1 または 2）。不正な値は無視する
    motor = obj.get('motor')
    if motor not in motor_ins:
        return

    cmd = obj.get('cmd')
    if cmd == 'on':
        motor_on[motor] = True
        apply_motor_state(motor)
    elif cmd == 'off':
        motor_on[motor] = False
        apply_motor_state(motor)
    elif cmd == 'dir':
        try:
            new_dir = int(obj.get('dir'))
        except (TypeError, ValueError):
            return
        if new_dir not in (0, 1):
            return
        motor_dir[motor] = new_dir
        if motor_on[motor]:
            # 回転中に方向を切り替えた場合は、その場でIN1/IN2の割り当てを更新する
            apply_motor_state(motor)
    elif cmd == 'set':
        try:
            new_value = int(obj.get('value', max_speed[motor]))
        except (TypeError, ValueError):
            return
        max_speed[motor] = max(0, min(65535, new_value))
        if motor_on[motor]:
            # onの最中であれば、スライドバーの動きをその場でモーターに反映する
            apply_motor_state(motor)

# WebSocket接続中のメインループ。接続が切れるまでここに留まる
def ws_loop(client):
    print('WS connected')
    client.setblocking(False)  # 受信待ちで固まらないよう非ブロッキングにする

    while True:
        try:
            data = client.recv(256)
            if data == b'':
                # 相手が正常にコネクションを閉じた
                break
            if data:
                frame = decode_ws_frame(data)
                if frame is not None:
                    opcode, payload = frame
                    if opcode == 0x8:
                        # クローズフレーム
                        break
                    elif opcode == 0x1:
                        try:
                            handle_command(payload.decode())
                        except Exception:
                            pass
                    # ping(0x9)やバイナリなど、今回使わない種類は無視
        except OSError:
            # 非ブロッキングでデータが無いときにここに来る（正常）
            pass

        utime.sleep_ms(20)  # CPUを回しすぎないための小休止

    print('WS disconnected')
    client.close()

# ヘッダーの終端（\r\n\r\n）が来るまで、または上限バイト数に達するまで読み続ける。
# ブラウザからのWebSocketアップグレードリクエストはヘッダーが多く、
# 1回のrecv(1024)では途中で切れてSec-WebSocket-Keyなどが欠けることがあるため、
# 必ずヘッダー全体を受け取ってから解析する
def recv_http_headers(client, max_bytes=2048):
    data = b''
    while b'\r\n\r\n' not in data and len(data) < max_bytes:
        try:
            chunk = client.recv(512)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data

# サーバを立ち上げる
def serve(connection):
    while True:
        client = connection.accept()[0]
        client.settimeout(3)  # 最初のリクエスト受信だけタイムアウトを付ける
        try:
            request = recv_http_headers(client)
        except OSError:
            client.close()
            continue

        if not request:
            client.close()
            continue

        text = request.decode('utf-8', 'ignore')
        lines = text.split('\r\n')
        try:
            request_line = lines[0].split()[1]
        except IndexError:
            client.close()
            continue

        path = request_line.split('?')[0]
        headers = parse_headers(lines[1:])
        print('Request:', path, '| Upgrade header:', headers.get('upgrade'))

        if path == '/ws' and headers.get('upgrade', '').lower() == 'websocket':
            if do_ws_handshake(client, headers):
                ws_loop(client)  # 接続が切れるまでここで専任処理する
            else:
                print('WS handshake failed. headers:', headers)
                client.close()
        else:
            send_response(client, build_html(), 'text/html')
            client.close()


try:
    ip = host(ssid, pw)
    connection = open_socket(ip)
    serve(connection)
except KeyboardInterrupt:
    machine.reset()
