#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TinyGPS++.h>
#include <HardWareSerial.h>
#include <Adafruit_BNO055.h>
#include <Ticker.h>
#include <math.h>
#define BNO055interval 10  //ミリ秒

Ticker bno055ticker;  //タイマー割り込み用のインスタンス

//GPSと9軸用変数
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);
TinyGPSPlus gps;
HardwareSerial MyGPS(1);
double Target_lat = 35.9500610;   //目的地の緯度
double Target_lon = 139.6537761;  //目的地の経度
double Current_lat;               //現在の緯度
double Current_lon;               //現在の経度
double Current_Yaw;               //現在の方位角
double Target_Yaw;                //目的地までの方位
double Target_Distance = 50;      //目的地までの距離(m)
double Yaw_Error;                 //方位の差
int Count_Gps = 0;                //GPS取得回数
double error_base = 20.0;

/*モータドライバー(TB6643kq)のピン設定*/
int RM_IN1 = 13;  //右のモータードライバー
int RM_IN2 = 14;
int LM_IN1 = 32;  //左のモータドライバー
int LM_IN2 = 33;

/*Lidar(TSD10)のピン設定*/
int RXD2 = 16;  // TSD10側のTX, 左端
int TXD2 = 17;  // TDS10側のRX, 右端

// キューのハンドラ (Core0 -> Core1のデータ伝達用)
QueueHandle_t xLidarQueue;

uint16_t Current_Distance_L = 5000;  //リトルエンディアンで距離を合成(mm単位)
int Goal_count = 0;

void setup() {
  pinMode(21, INPUT_PULLUP);  //SDA 21番ピンのプルアップ(BNO)
  pinMode(22, INPUT_PULLUP);  //SCL 22番ピンのプルアップ(BNO)
  /*モータドライバー設定*/
  pinMode(RM_IN1, OUTPUT);
  pinMode(RM_IN2, OUTPUT);
  pinMode(LM_IN1, OUTPUT);
  pinMode(LM_IN2, OUTPUT);

  Serial.begin(115200);
  while (!bno.begin())  // センサの初期化
  {
    Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
    delay(1000);
  }

  bno.setExtCrystalUse(false);

  Serial.println("Calibration status values: 0=uncalibrated, 3=fully calibrated");

  MyGPS.begin(9600, SERIAL_8N1, 26, 27);
  Serial2.begin(460800, SERIAL_8N1, RXD2, TXD2);
}


void loop() {
  while (Target_Distance >= 5.0) {
    while (MyGPS.available() > 0) {
      if (gps.encode(MyGPS.read())) {
        if (gps.location.isValid()) {
          Current_lat = gps.location.lat();
          Current_lon = gps.location.lng();
        }
        imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
        // BNO055のデータは現在の方位角なのでCurrent_Yawに代入
        Current_Yaw = euler.x();
        Target_Distance = gps.distanceBetween(Current_lat, Current_lon, Target_lat, Target_lon);
        Target_Yaw = gps.courseTo(Current_lat, Current_lon, Target_lat, Target_lon);
        Yaw_Error = Target_Yaw - Current_Yaw;
        //方角の偏差を-180から180の間に修正
        if (Yaw_Error > 180) {
          Yaw_Error -= 360;
        } else if (Yaw_Error < -180) {
          Yaw_Error += 360;
        }
        //PID制御かPivot制御かを選択
        if (abs(Yaw_Error) >= error_base) {
          Pivot(Yaw_Error);
        } else {
          Pid(Yaw_Error);
        }
        delay(100);
      }
    }
  }
  while (Current_Distance_L >= 50 && Target_Distance >= 5) {
    if (gps.encode(MyGPS.read())) {
      Current_lat = gps.location.lat();
      Current_lon = gps.location.lng();
      imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
      // BNO055のデータは現在の方位角なのでCurrent_Yawに代入
      Current_Yaw = euler.x();
      Target_Distance = gps.distanceBetween(Current_lat, Current_lon, Target_lat, Target_lon);
      if (Target_Distance > 5.0) {
        break;
      }
      if (Serial2.available() >= 4) {
        uint8_t header = Serial2.read();
        // フレームヘッダ 0x5C を確認
        if (header == 0x5C) {
          uint8_t distLow = Serial2.read();
          uint8_t distHigh = Serial2.read();
          uint8_t checkSum = Serial2.read();

          // チェックサム計算 (2～3バイト目の和の否定) [cite: 97-107]
          uint8_t calculatedSum = ~(distLow + distHigh);

          if (calculatedSum == checkSum) {
            // リトルエンディアンで距離を合成 (mm単位)
            Current_Distance_L = distLow | (distHigh << 8);

            if (Current_Distance_L == 65535) {
              Current_Distance_L = 500;
            }
          }
        }
        if (Current_Distance_L >= 500) {
          digitalWrite(LM_IN1, LOW);
          digitalWrite(LM_IN2, HIGH);
          digitalWrite(RM_IN1, HIGH);
          digitalWrite(RM_IN2, LOW);
          delay(200);
        } else {
          digitalWrite(LM_IN1, HIGH);
          digitalWrite(LM_IN2, LOW);
          digitalWrite(RM_IN1, HIGH);
          digitalWrite(RM_IN2, LOW);
          delay(200);
        }
      }
    }
  }
}

void Pivot(double Yaw_Error) {
  // ループで止めず、一瞬だけモーターを回してすぐ抜ける（全体ループで再計測させる）
  if (Yaw_Error > 0) {
    // 右回り
    digitalWrite(LM_IN1, HIGH);
    digitalWrite(LM_IN2, LOW);
    digitalWrite(RM_IN1, LOW);  // 逆回転させる場合はHIGH/LOW調整
    digitalWrite(RM_IN2, HIGH);
  } else {
    // 左回り
    digitalWrite(LM_IN1, LOW);
    digitalWrite(LM_IN2, HIGH);
    digitalWrite(RM_IN1, HIGH);
    digitalWrite(RM_IN2, LOW);
  }
}

void Pid(double Yaw_Error) {
  // 比例ゲイン(P制御)
  int pwm_val = abs(Yaw_Error) * 10;     // 数値は実際の動きを見て調整
  pwm_val = constrain(pwm_val, 0, 100);  // PWM上限を超えないよう制限

  // 基準速度（前進ベース）
  int base_speed = 150;
  int right_speed = base_speed;
  int left_speed = base_speed;

  // 誤差に応じた速度差分を計算
  if (Yaw_Error > 0) {
    right_speed -= pwm_val;  // 右を遅くして右へ曲がる
    left_speed += pwm_val;
  } else {
    right_speed += pwm_val;
    left_speed -= pwm_val;  // 左を遅くして左へ曲がる
  }

  // 最終的なPWM値の制約
  right_speed = constrain(right_speed, 0, 255);
  left_speed = constrain(left_speed, 0, 255);

  analogWrite(RM_IN1, right_speed);
  analogWrite(RM_IN2, 0);
  analogWrite(LM_IN1, left_speed);
  analogWrite(LM_IN2, 0);
}
