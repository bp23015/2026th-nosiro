#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <HardwareSerial.h>

/*Lidar(TSD10)のピン設定*/
int RXD2 = 16;    // TSD10側のTX, 左端
int TXD2 = 17;    // TDS10側のRX

Adafruit_BME280 bme;     // SDA(SDI)21, SCL(SCK)22

// キューのハンドラ (Core0 -> Core1のデータ伝達用)
QueueHandle_t xPressureQueue;
QueueHandle_t xLidarQueue;

volatile int subtask = 0;  // Core0で順に実行する
const int subtask_amount = 2; // サブタスクの作成量

int Current_Distance_L = 1;
float Current_Pressure = 0;

const int data_amount = 10; // Slopeのデータ取得量
const int sensor_dist = 30; // センサ値の取得間隔
const float base = 0.5;     // Slopeの閾値、暫定
// const float bme_reset_threshold = 700;  // bmeのエラー検出閾値, 調整中

struct Slope{
  float a;      // 近似直線の傾き
  float b;      // 近似直線の切片
};


void setup(){
  Serial.begin(115200);
  
  // TSD10とのUART通信 (デフォルトの 460800 bps に設定)
  Serial2.begin(468000, SERIAL_8N1, RXD2, TXD2);

  // BME280とのI2C通信
  Wire.begin(21, 22); // SDA(SDI), SCL(SCK)
  bool status;
  status = bme.begin(0x76);
  while (!status) {
    Serial.println("BME280 sensorが使えません");
    delay(1000);
  }

  // 長さ1のキューを作成 (1データのみ保持)
  xLidarQueue = xQueueCreate(1, sizeof(int));
  xPressureQueue = xQueueCreate(1, sizeof(float));

  // Core0でタスク起動
  xTaskCreatePinnedToCore(
    vSensorTask,    // 作成するタスク関数
    "vSensorTask",  // 表示用タスク名
    4096,           // スタックメモリ量
    NULL,           // 起動パラメータ
    2,              // 優先度 (loopの1より高く)
    NULL,           // タスクハンドラ
    0               // 実行するコア
  );

  // Core1でタスク起動
  xTaskCreatePinnedToCore(
    vRestoreTask,    // 作成するタスク関数
    "vRestoreTask",  // 表示用タスク名
    4096,           // スタックメモリ量
    NULL,           // 起動パラメータ
    2,              // 優先度 (loopの1より高く)
    NULL,           // タスクハンドラ
    1               // 実行するコア
  );

  delay(1000);
}

void loop(){
  int current_trend = get_trend(base);
  Serial.print("trend status: ");
  Serial.println(current_trend);
  Serial.println();
}


/* Core0で各センサの値を順に取得する */
void vSensorTask(void *pvParameters) {
  while (1) {
    int dist;
    float pressure;

    if (subtask == 2) subtask = 0;

    switch(subtask){
      // Lidarで距離を取得
      case 0: 
        while (Serial2.available() >= 4) {
          // 正常に取得できた最新値のみを上書き
          if (get_dist(&dist)) {
            xQueueOverwrite(xLidarQueue, &dist);
          }
        }
        break;

      // bmeで気圧を取得
      case 1: 
        pressure = bme.readPressure() / 100.0F;
        xQueueOverwrite(xPressureQueue, &pressure);
        break;

      default:
        break;
    }

    subtask++;
    vTaskDelay(pdMS_TO_TICKS(sensor_dist));  // WDTクリア
  }
}

/* Core1でセンサの値を格納する */
void vRestoreTask(void *pvParameters) {
  while (1) {
    // データが届いていれば取り出す、なければスルー
    if (xQueueReceive(xLidarQueue, &Current_Distance_L, 0) == pdPASS) {
      if (Current_Distance_L == 65535){
        Serial.println("Distance: Error(Out of range)");
      } else {
        Serial.print("Distance: ");
        Serial.print(Current_Distance_L);
        Serial.println(" mm");
      }
    }
    
    if (xQueueReceive(xPressureQueue, &Current_Pressure, 0) == pdPASS) {
      Serial.print("Pressure: ");
      Serial.print(Current_Pressure);
      Serial.println(" hPa");

      // エラーが起きたら再起動, 調整中
      // if (Current_Pressure < bme_reset_threshold) {
      //   Serial.println("==BME ERROR==");
      //   bme.begin(0x76);
      //   vTaskDelay(pdMS_TO_TICKS(1000));
      // }
    }

    vTaskDelay(pdMS_TO_TICKS(sensor_dist));  // WDTクリア
  }
}

/* Lidarで目的地までの距離を取得する */
bool get_dist(int *out_distance) {
  while (Serial2.available() > 0 && Serial2.peek() != 0x5C) {
    Serial2.read(); // 先頭が0x5Cではないので破棄
  }

  if (Serial2.available() < 4) {
    return false; // 4バイト未満なら何もしない
  }

  uint8_t header = Serial2.read();
  uint8_t distLow = Serial2.read();
  uint8_t distHigh = Serial2.read();
  uint8_t checkSum = Serial2.read();
  
  // チェックサム計算
  uint8_t calculatedSum = ~(distLow + distHigh);
  
  if (calculatedSum == checkSum) {
    uint16_t distance = distLow | (distHigh << 8);
    *out_distance = (int)distance;
    return true;
  }
  // ヘッダが違った場合、またはチェックサムエラー時は false を返す
  return false; 
}

/* 近似直線の傾きと切片を求める */
Slope get_slope(float datas[], int data_length, float mean_i, float mean_d){
  Slope slope;
  float covariance = 0;                   // インデックスとデータの共分散(*データ数)
  float variance_i = 0;                   // インデックスの分散(*データ数)

  for (int i=0; i<data_amount; i++) {
    float res_i = i - mean_i;             // インデックスの偏差
    float res_d = datas[i] - mean_d;      // データの偏差
    covariance += res_i * res_d;
    variance_i += res_i * res_i;
  }
  slope.a = covariance / variance_i;      // 傾き
  slope.b = mean_d - slope.a * mean_i;    // 切片

  return slope;
}

/* 現在の上昇状態を求める */
int get_trend(float base) {
  // 気圧を取得し、インデックスと気圧の平均値を求める
  float pressures[data_amount];           // 気圧データ
  float mean_i = (data_amount - 1) / 2;   // インデックスの平均値 (除外がないのでデータ数/2)
  float mean_p = 0;                       // 気圧の平均値
  // Serial.print("pressures: ");

  for (int i=0; i<data_amount; i++) {
    pressures[i] = Current_Pressure / 100.0F;
    mean_p += pressures[i];

    // Serial.print(pressures[i]);
    // Serial.print(", ");
    delay(sensor_dist * subtask_amount);                   // センサの取得間隔
  }
  mean_p /= data_amount;

  // Serial.println();
  // Serial.print("mean_p: ");
  // Serial.println(mean_p);


  // 気圧の近似直線の傾き、切片を求める
  Slope slope_p = get_slope(pressures, data_amount, mean_i, mean_p);
  // Serial.print("slope: ");
  // Serial.print(slope_p.a);
  // Serial.print(", ");
  // Serial.println(slope_p.b);

  // 近似直線との残差を求め、残差の標準偏差も求める
  float p_residuals[data_amount];         // 近似直線との残差
  float sd_res;                           // 残差の標準偏差

  // Serial.print("p_res: ");
  for (int i=0; i<data_amount; i++) {
    p_residuals[i] = pressures[i] - (slope_p.a * i + slope_p.b);
    sd_res += p_residuals[i] * p_residuals[i];
    
    // Serial.print(p_residuals[i]);
    // Serial.print(", ");
  }
  sd_res = sqrt(sd_res / data_amount);
  
  // Serial.println();
  // Serial.print("sd_res: ");
  // Serial.println(sd_res);

  // 外れ値を除いた気圧データで平均値を求める
  mean_i = 0, mean_p = 0;                 // 平均値をリセット
  
  // Serial.print("Except: ");
  for (int i=0; i<data_amount; i++) {
    // 残差の絶対値が標準偏差の3倍を超えるなら除外
    if (abs(p_residuals[i]) > 3*sd_res) {
      // Serial.print(p_residuals[i]);
      // Serial.print(", ");

      pressures[i] -= p_residuals[i];     // 外れ値なら近似直線上の値として扱う
    }
    mean_i += i;
    mean_p += pressures[i];
  }
  mean_i /= data_amount;
  mean_p /= data_amount;

  // Serial.println();
  // Serial.print("mean_p: ");
  // Serial.println(mean_p);

  // 外れ値を除いた気圧データで傾きを求める
  Slope slope_fixed = get_slope(pressures, data_amount, mean_i, mean_p);
  Serial.println();
  Serial.print("slope_fixed: ");
  Serial.print(slope_fixed.a);
  Serial.print(", ");
  Serial.println(slope_fixed.b);

  // 傾きの絶対値と閾値baseの差、傾きの正負の2つを求め、上昇なら1、安定なら0、下降なら-1を返す (高度と気圧は反比例)
  if (abs(slope_fixed.a) < base)  return  0;  // 安定
  else if (slope_fixed.a < 0)     return  1;  // 上昇
  else                            return -1;  // 下降
}

/* 機体の上昇状態がbaseになるまで待機する */
void compare(float base) {
  while (1) {
    if (get_trend(base)) break;
  }
}