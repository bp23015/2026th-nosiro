#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <Ticker.h>
#include <math.h>
Ticker bno055ticker;       //タイマー割り込み用のインスタンス
#define BNO055interval 10  //何ms間隔でデータを取得するか

//Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire); //ICSの名前, デフォルトアドレス, 謎
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);
//GPSのオブジェクト
TinyGPSPlus gps;
// GPS用にSerial1、またはSerial2のピンをずらして定義
HardwareSerial MyGPS(1);

double Target_lat;            //目的地の緯度
double Target_lon;            //目的地の経度
double Current_lat;           //現在の緯度
double Current_lon;           //現在の経度
double Current_Yaw;           //現在の方位角
double Target_Yaw;            //目的地までの方位
double Target_Distance = 50;  //目的地までの距離(m)
double Yaw_Error;             //方位の差
double base = 20;

/*モータドライバー(TB6643kq)のピン設定*/
int RM_IN1 = 13;  //右のモータードライバー
int RM_IN2 = 14;
int LM_IN1 = 26;  //左のモータドライバー
int LM_IN2 = 25;

void setup() {
  pinMode(21, INPUT_PULLUP);  //SDA 21番ピンのプルアップ(BNO)
  pinMode(22, INPUT_PULLUP);  //SCL 22番ピンのプルアップ(BNO)
  /*モータドライバー設定*/
  pinMode(RM_IN1, OUTPUT);
  pinMode(RM_IN2, OUTPUT);
  pinMode(LM_IN1, OUTPUT);
  pinMode(LM_IN2, OUTPUT);

  Serial.begin(115200);
  Serial.println("Orientation Sensor Raw Data Test");
  Serial.println("");

  while (!bno.begin())  // センサの初期化
  {
    Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
    delay(1000);
  }

  delay(1000);

  /* Display the current temperature */
  int8_t temp = bno.getTemp();
  Serial.print("Current Temperature: ");
  Serial.print(temp);
  Serial.println(" C");
  Serial.println("");

  bno.setExtCrystalUse(false);

  Serial.println("Calibration status values: 0=uncalibrated, 3=fully calibrated");

  MyGPS.begin(9600, SERIAL_8N1, 26, 27);

  Set_Calibration();  // bnoのキャリブレーション
}

void loop() {
  while (Target_Distance >= 2.0) {
    while (MyGPS.available() > 0) {
      if (gps.encode(MyGPS.read())) {
        GetData();
        Calc_Dist();
        Yaw_Error = Calc_Error(Target_Yaw, Current_Yaw);
        if (Mode_Select(Yaw_Error)) {
          Pivot(Yaw_Error);
        } else {
          Pid(Yaw_Error);
        }
      }
    }
  }
}

void GetData() {
  Current_lat = gps.location.lat();
  Current_lon = gps.location.lng();
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  Target_Yaw = euler.x();
}

void Calc_Dist() {
  Target_Distance = gps.distanceBetween(Current_lat, Current_lon, Target_lat, Target_lon);
  Target_Yaw = gps.courseTo(Current_lat, Current_lon, Target_lat, Target_lon);
}

double Calc_Error(double Target_Yaw, double Current_Yaw) {
  Yaw_Error = Target_Yaw - Current_Yaw;  //目的地-現在の方位
  if (Yaw_Error > 180) {
    Yaw_Error -= 360;
  } else if (Yaw_Error < -180) {
    Yaw_Error += 360;
  }
  return Yaw_Error;
}

bool Mode_Select(double Yaw_Error) {
  if (abs(Yaw_Error) >= base) {
    return true;
  } else {
    return false;
  }
}

void Pivot(double Yaw_Error) {
  if (Yaw_Error <= 0) {
    digitalWrite(LM_IN1, HIGH);
    digitalWrite(LM_IN2, LOW);
    digitalWrite(RM_IN1, LOW);
    digitalWrite(RM_IN2, HIGH);
  } else {
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

void Display(double something) {
  Serial.println(something, 6);
}

void Set_Calibration() {
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
  delay(1000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(RM_IN1, HIGH);
  digitalWrite(RM_IN2, LOW);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
  delay(2000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
  delay(1000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(RM_IN1, HIGH);
  digitalWrite(RM_IN2, LOW);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
  delay(3000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(RM_IN1, HIGH);
  digitalWrite(RM_IN2, LOW);
  delay(1000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  digitalWrite(RM_IN1, HIGH);
  digitalWrite(RM_IN2, LOW);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
  delay(2000);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
}