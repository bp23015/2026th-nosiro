void Goal_decision() {
  while (MyGPS.available() > 0) {
    if (gps.encode(MyGPS.read())) {
      GetData();
      Calc_Dist();
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
              Serial.println("Status: Out of Range");
            } else {
              Serial.print("Distance: ");
              Serial.print(Current_Distance_L);
              Serial.println(" mm");
              Goal_count++;
              SD_Append_Lidar(Target_Distance,Current_Distance_L,(String)Goal_count);
            }
          }
        }
      }
    }
  }
}