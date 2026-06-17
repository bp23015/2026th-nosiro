#include <esp_task_wdt.h>
#include <HardwareSerial.h>

/*Lidar(TSD10)のピン設定*/
int RXD2 = 16;    // TSD10側のTX, 左端
int TXD2 = 17;    // TDS10側のRX, 右端

// キューのハンドラ (Core0 -> Core1のデータ伝達用)
QueueHandle_t xLidarQueue;

int Current_Distance_L = 1;

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

void vLidarTask(void *pvParameters) {
  while (1) {
    int dist;

    while (Serial2.available() >= 4) {
      // 正常に取得できた最新値のみを上書き
      if (get_dist(&dist)) {
        xQueueOverwrite(xLidarQueue, &dist);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));  // WDTクリア
  }
}

void setup(){
  Serial.begin(115200);
  
  // TSD10との通信 (デフォルトの 460800 bps に設定)
  Serial2.begin(468000, SERIAL_8N1, RXD2, TXD2);

  // 長さ1のキューを作成 (1データのみ保持)
  xLidarQueue = xQueueCreate(1, sizeof(int));

  // Core0でタスク起動
  xTaskCreatePinnedToCore(
    vLidarTask,     // 作成するタスク関数
    "vLidarTask",   // 表示用タスク名
    4096,           // スタックメモリ量
    NULL,           // 起動パラメータ
    2,              // 優先度 (loopの1より高く)
    NULL,           // タスクハンドラ
    0               // 実行するコア
  );

  delay(1000);
}

void loop(){
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
  
  delay(10);
}