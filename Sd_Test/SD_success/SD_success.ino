#include <SD.h>

const char* fname = "/ESP32.txt";
File file;

void setup() {
  Serial.begin(115200);
  if (!SD.begin(5)) {
    Serial.println("Card MOunt failed");
    return;
  }
  SD_Write();
  for (int i = 0; i < 10; i++) {
    SD_Append(String(300 + i));
  }
  SD_Read();
  SD_ListDir();
  // put your setup code here, to run once:
}

void loop() {
  // put your main code here, to run repeatedly:
}

void SD_Write() {
  file = SD.open(fname, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println("200");
  file.println("500");
  file.close();
  delay(10);
}

void SD_Append(String loc) {
  file = SD.open(fname, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println(loc);
  file.close();
  delay(10);
}

void SD_Read() {
  file = SD.open(fname);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void SD_ListDir(){
  File root=SD.open("/");
  file =root.openNextFile();
  while(file){
    Serial.println(file.name());
    file=root.openNextFile();
  }
}
