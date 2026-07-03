/*ファイルに書き込む(書き込みするファイルを指定)*/
void SD_Write_BME() {
  file = SD.open(fname1, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  for (int i = 0; i < data_amount; i++) {
    file.printf("p%d,", i);
  }
  file.print("slope,");
  file.print("speed,");
  file.print("judgement,");
  file.println("number");
  file.close();
  delay(10);
}

void SD_Append_BME(float datas[14]) {  //0~9: 気圧, 10: 傾き, 11: 上昇速度, 12: 判定, 13: number
  file = SD.open(fname1, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }

  for (int i = 0; i < 13; i++) {
    char charbuf[15];
    dtostrf(datas[i], 0, 7, charbuf);
    String str = String(charbuf);
    file.print(str + ",");
  }

  file.println(append_datas[13]);
  file.close();
  delay(10);
}

void SD_Write_GPS() {
  file = SD.open(fname2, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.print("latitude,");
  file.print("longitude,");
  file.println("number");
  file.close();
  delay(10);
}

void SD_Append_GPS(double lat, double lon, String number) {
  char charbuf[15];
  char charbuf2[15];
  dtostrf(lat, 0, 7, charbuf);
  String latstr = String(charbuf);
  dtostrf(lon, 0, 7, charbuf2);
  String lonstr = String(charbuf2);
  file = SD.open(fname2, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.print(latstr + ",");
  file.print(lonstr + ",");
  file.println(number);
  file.close();
  delay(10);
}

void SD_Write_Lidar() {
  file = SD.open(fname3, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.print("GPS Distance,");
  file.print("Lidar Distance,");
  file.println("number");
  file.close();
  delay(10);
}

void SD_Append_Lidar(double GPS, int Lidar, String number) {
  char charbuf[15];
  // char charbuf2[15];?
  dtostrf(GPS, 0, 7, charbuf);
  String gpsstr = String(charbuf);
  String Lidarstr = String(Lidar);
  file = SD.open(fname3, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.print(gpsstr + ",");
  file.print(Lidarstr + ",");
  file.println(number);
  file.close();
  delay(10);
}