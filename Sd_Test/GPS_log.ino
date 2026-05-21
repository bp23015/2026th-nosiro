#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <SD.h>

TinyGPSPlus gps;
// GPS用にSerial1、またはSerial2のピンをずらして定義
HardwareSerial MyGPS(1);
File file;
const char* fname = "/ESP32-2.txt";

// 目的地（芝浦工大 大宮キャンパス付近）
double target_lat = 35.949923;
double target_lng = 139.653681;

void setup() {
  Serial.begin(115200);
  // LiDARが16,17を使うなら、GPSは27(RX), 26(TX)などに設定
  MyGPS.begin(9600, SERIAL_8N1, 26, 27);

  Serial.println("GPS Test Start...");
  while(!SD.begin(5)) {
    Serial.println("カードが見つけられません");
    delay(1000);
  }
  file = SD.open(fname, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println("ここから履歴");
  file.close();
}

void loop() {
  // 1. GPSデータの解析
  while (MyGPS.available() > 0) {
    if (gps.encode(MyGPS.read())) {
      displayInfo();
    }
  }

  // 10秒経っても何も受信しない場合は配線ミスを疑う
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println("GPSモジュールが見つかりません。配線を確認してください。");
    delay(5000);
  }
}

void displayInfo() {
  if (gps.location.isUpdated()) {
    double current_lat = gps.location.lat();
    double current_lng = gps.location.lng();

    double distance = gps.distanceBetween(current_lat, current_lng, target_lat, target_lng);
    double course = gps.courseTo(current_lat, current_lng, target_lat, target_lng);

    // Serial.print("Dist: "); Serial.println(current_lat,6);
    // Serial.print("Target Course: "); Serial.println(current_lng,6);

    Serial.print("Dist: ");
    Serial.print(distance);
    Serial.println("m ");
    Serial.print("Target Course: ");
    Serial.print(course);
    Serial.println(" deg");
    file = SD.open(fname, FILE_APPEND);
    if (!file) {
      Serial.println("Failed to open file");
      return;
    }
    file.print("目的地までの距離；");
    file.println(distance);
    file.print("目的地までの角度：");
    file.println(course);
    file.close();
  }
}