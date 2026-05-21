#include <Wire.h>
#include <SD.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
Adafruit_BME280 bme;

const char* fname = "/pressure_log.txt";
File file;

const int data_amount = 10;
const int sensor_dist = 50;

const float base = 0.5;   // 暫定

struct Slope{
  float a;      // 近似直線の傾き
  float b;      // 近似直線の切片
};

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22); // SDA(SDI), SCL(SCK)
  bool status;
  status = bme.begin(0x76);
  while (!status) {
    Serial.println("BME280 sensorが使えません");
    delay(1000);
  }

  if (!SD.begin(5)) {
    Serial.println("Card Mount failed");
    return;
  }
  SD_Write("Logger Start");

}

void loop() { 
  float pressure = bme.readPressure() / 100.0F;
  SD_Append_float(pressure);
  Serial.println(pressure);
  delay(100);
}


/*ファイルに書き込む(書き込みするファイルを指定)*/
void SD_Write(String message) {
  file = SD.open(fname, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println(message);
  file.close();
  delay(10);
}

/*ファイルにテキストを追記する*/
void SD_Append_str(String loc) {
  file = SD.open(fname, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println(loc);
  file.close();
  delay(10);
}

/*ファイルにfloat値を追記する*/
void SD_Append_float(float loc) {
  file = SD.open(fname, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  file.println(loc);
  file.close();
  delay(10);
}

/*ファイルの内容をシリアルモニタに表示する*/
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

/*SDカードにあるファイルの名前を表示する*/
void SD_ListDir(){
  File root=SD.open("/");
  file =root.openNextFile();
  while(file){
    Serial.println(file.name());
    file=root.openNextFile();
  }
}
