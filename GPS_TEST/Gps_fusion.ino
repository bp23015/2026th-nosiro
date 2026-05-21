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
double base=20;

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
}

void loop() {
  Set_Calibration();
  while (Target_Distance <= 2.0) {
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
    Yaw_Error - 360;
  } else if (Yaw_Error < -180) {
    Yaw_Error + 360;
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
  while (Yaw_Error <= 10) {
    if (Yaw_Error <= 0) {
      digitalWrite(LM_IN1, HIGH);
      digitalWrite(LM_IN2, LOW);
      delay(100);
    } else {
      digitalWrite(RM_IN1, HIGH);
      digitalWrite(RM_IN2, LOW);
      delay(100);
    }
  }
}

void Pid(double Yaw_Error) {
  if (Yaw_Error > 0) {
    analogWrite(RM_IN1, 60 * Yaw_Error);
    analogWrite(RM_IN2, 0);
  } else {
    analogWrite(LM_IN1, 60 * Yaw_Error);
    analogWrite(LM_IN2, 0);
  }
  digitalWrite(RM_IN1, HIGH);
  digitalWrite(RM_IN2, LOW);
  digitalWrite(LM_IN1, HIGH);
  digitalWrite(LM_IN2, LOW);
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