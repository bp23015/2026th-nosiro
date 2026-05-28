/*
 * TSD10 DTOF LiDAR テストプログラム (ESP32 / UART2使用)
 */

#define RXD2 16
#define TXD2 17

void setup() {
  Serial.begin(115200); // PCへの出力用
  // TSD10との通信 (デフォルトの 460800 bps に設定)
  Serial2.begin(460800, SERIAL_8N1, RXD2, TXD2); 
  
  Serial.println("TSD10 LiDAR Test Start...");
}

void loop() {
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
        uint16_t distance = distLow | (distHigh << 8);
        
        if (distance == 65535) {
          Serial.println("Status: Out of Range");
        } else {
          Serial.print("Distance: ");
          Serial.print(distance);
          Serial.println(" mm");
        }
      }
    }
  }
}