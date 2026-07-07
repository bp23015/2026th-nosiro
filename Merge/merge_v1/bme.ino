#include <math.h>

void Landing_Check() {
  append_datas[12] = get_trend(base);
  append_datas[13] = count;
  SD_Append_BME(append_datas);
  count++;
}

slope get_slope(float datas[], int data_length, float mean_i, float mean_d) {  // (配列、配列の長さ、インデックス平均、データ平均)
  slope slope;
  float covariance = 0;  // インデックスとデータの共分散(*データ数)
  float variance_i = 0;  // インデックスの分散(*データ数)

  for (int i = 0; i < data_amount; i++) {
    float res_i = i - mean_i;         // インデックスの偏差
    float res_d = datas[i] - mean_d;  // データの偏差
    covariance += res_i * res_d;
    variance_i += res_i * res_i;
  }
  slope.a = covariance / variance_i;    // 傾き
  slope.b = mean_d - slope.a * mean_i;  // 切片

  return slope;
}

/* 現在の上昇状態を求める */
int get_trend(float base) {
  // 気圧を取得し、インデックスと気圧の平均値を求める
  float pressures[data_amount];          // 気圧データ
  float mean_i = (data_amount - 1) / 2;  // インデックスの平均値 (除外がないのでデータ数/2)
  float mean_p = 0;                      // 気圧の平均値
  Serial.print("pressures: ");

  for (int i = 0; i < data_amount; i++) {
    pressures[i] = bme.readPressure() / 100.0F;
    append_datas[i] = pressures[i];
    mean_p += pressures[i];

    Serial.print(pressures[i]);
    Serial.print(", ");
    delay(sensor_dist);  // センサの取得間隔
  }
  mean_p /= data_amount;

  Serial.println();
  Serial.print("mean_p: ");
  Serial.println(mean_p);


  // 気圧の近似直線の傾き、切片を求める
  slope slope_p = get_slope(pressures, data_amount, mean_i, mean_p);
  Serial.print("slope: ");
  Serial.print(slope_p.a);
  Serial.print(", ");
  Serial.println(slope_p.b);

  // 近似直線との残差を求め、残差の標準偏差も求める
  float p_residuals[data_amount];  // 近似直線との残差
  float sd_res;                    // 残差の標準偏差

  Serial.print("p_res: ");
  for (int i = 0; i < data_amount; i++) {
    p_residuals[i] = pressures[i] - (slope_p.a * i + slope_p.b);
    sd_res += p_residuals[i] * p_residuals[i];

    Serial.print(p_residuals[i]);
    Serial.print(", ");
  }
  sd_res = sqrt(sd_res / data_amount);

  Serial.println();
  Serial.print("sd_res: ");
  Serial.println(sd_res);

  // 外れ値を除いた気圧データで平均値を求める
  mean_i = 0, mean_p = 0;  // 平均値をリセット

  Serial.print("Except: ");
  for (int i = 0; i < data_amount; i++) {
    // 残差の絶対値が標準偏差の3倍を超えるなら除外
    if (abs(p_residuals[i]) > 3 * sd_res) {
      Serial.print(p_residuals[i]);
      Serial.print(", ");

      pressures[i] -= p_residuals[i];  // 外れ値なら近似直線上の値として扱う
    }
    mean_i += i;
    mean_p += pressures[i];
  }
  mean_i /= data_amount;
  mean_p /= data_amount;

  Serial.println();
  Serial.print("mean_p: ");
  Serial.println(mean_p);

  // 外れ値を除いた気圧データで傾きを求める
  slope slope_fixed = get_slope(pressures, data_amount, mean_i, mean_p);
  Serial.print("slope_fixed: ");
  Serial.print(slope_fixed.a);
  Serial.print(", ");
  Serial.println(slope_fixed.b);

  float speed = slope_fixed.a * press2height / (sensor_dist / 1000.0);  // 上昇速度[m/s]を傾きから求める
  Serial.print("speed: ");
  Serial.println(speed);
  append_datas[10] = slope_fixed.a;
  append_datas[11] = speed;

  // 傾きの絶対値と閾値baseの差、傾きの正負の2つを求め、上昇なら1、安定なら0、下降なら-1を返す
  if (abs(speed) < base) return 0;  // 安定
  else if (speed > 0) return 1;     // 上昇
  else return -1;                   // 下降
}

/* 機体の上昇状態がbaseになるまで待機する */
void compare(float base) {
  while (1) {
    if (get_trend(base)) break;
  }
}
