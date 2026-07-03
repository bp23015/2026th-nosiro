#include <Wire.h>
#include <SD.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TinyGPS++.h>
#include <HardWareSerial.h>
#include <Adafruit_BNO055.h>
#include <Ticker.h>
#include <math.h>
#define BNO055interval 10

Adafruit_BME280 bme;
Ticker bno055ticker;  //タイマー割込み用のインスタンス

const char* fname1 = "/pressure'slog_20260705.csv";
const char* fname2 = "/Gps'slog_20260705.csv";
const char* fname3 = "/Goal'slog_20260705.csv";
File file;

//気圧センサ用変数
float append_datas[14];
int count = 0;
const int data_amount = 10;
const int sensor_dist = 50;
const float press2height = -10 / 1.2;
const float base = 0.5;
int Landing_Phase = 0;
struct slope {
  float a;
  float b;
};

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
double error_base=20.0;

/*モータドライバー(TB6643kq)のピン設定*/
int RM_IN1 = 13;  //右のモータードライバー
int RM_IN2 = 14;
int LM_IN1 = 32;  //左のモータドライバー
int LM_IN2 = 33;

/*Lidar(TSD10)のピン設定*/
int RXD2 = 16;    // TSD10側のTX, 左端
int TXD2 = 17;    // TDS10側のRX, 右端

// キューのハンドラ (Core0 -> Core1のデータ伝達用)
QueueHandle_t xLidarQueue;

uint16_t Current_Distance_L = 1; //リトルエンディアンで距離を合成(mm単位)
int Goal_count=0;
void setup() {

  pinMode(21, INPUT_PULLUP);  //SDA 21番ピンのプルアップ(BNO)
  pinMode(22, INPUT_PULLUP);  //SCL 22番ピンのプルアップ(BNO)
  /*モータドライバー設定*/
  pinMode(RM_IN1, OUTPUT);
  pinMode(RM_IN2, OUTPUT);
  pinMode(LM_IN1, OUTPUT);
  pinMode(LM_IN2, OUTPUT);

  Serial.begin(115200);
  Wire.begin(21, 22);
  bool status;
  while (!(bme.begin(0x76))) {
    Serial.println("気圧が使えません");
    delay(1000);
  }

  while (!bno.begin())  // センサの初期化
  {
    Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
    delay(1000);
  }


  while (!SD.begin(5)) {
    Serial.println("Card Mount failed");
    delay(1000);
  }
  SD_Write_BME();

  bno.setExtCrystalUse(false);

  Serial.println("Calibration status values: 0=uncalibrated, 3=fully calibrated");

  MyGPS.begin(9600, SERIAL_8N1, 26, 27);
  Serial2.begin(460800, SERIAL_8N1, RXD2, TXD2); 
  // put your setup code here, to run once:
}

void loop() {
  /*
  Landing_Phase
  0　：　キャリアに収納時、基本判定は0(安定)
  1　：　ドローン上昇時、判定1がn回で0から1になる
  2　：　ドローンが最高高度に到達、Landing_Phaseが1かつ判定0がn回で移行
  3　：　ドローン放出時、Landing_Phaseが2かつ判定-1がn回で移行
  4　：　安着陸時、Landing_Phaseが3かつ判定0がn回で移行

  これ以外にも、判定が失敗した時用に、時間で強制移行も入れる
  */
  while (!(append_datas[12] == 0 && Landing_Phase == 4)) {  //着地判定するまで
    Landing_Check();
  }
  SD_Write_GPS();
  static unsigned long lastSensorTime = 0;

  while (Target_Distance > 5.0) {
    Running();
  }

  SD_Write_Lidar();
  while(Target_Distance >5.0 && Current_Distance_L >100 ){
    Goal_decision();
  }

  while(1);

}
