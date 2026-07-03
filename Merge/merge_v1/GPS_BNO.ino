void Running() {
  while (MyGPS.available() > 0) {
    if (gps.encode(MyGPS.read())) {
      GetData();
      Calc_Dist();
      Count_Gps++;
      SD_Append_GPS(Current_lat, Current_lon, (String)count);
      Yaw_Error = Calc_Error(Target_Yaw, Current_Yaw);

      if (Mode_Select(Yaw_Error)) {
        Pivot(Yaw_Error);
      } else {
        Pid(Yaw_Error);
      }
    }
  }
}

void GetData() {
  if (gps.location.isValid()) {
    Current_lat = gps.location.lat();
    Current_lon = gps.location.lng();
  }
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  // BNO055のデータは現在の方位角なのでCurrent_Yawに代入
  Current_Yaw = euler.x();
}

void Calc_Dist() {
  Target_Distance = gps.distanceBetween(Current_lat, Current_lon, Target_lat, Target_lon);
  Target_Yaw = gps.courseTo(Current_lat, Current_lon, Target_lat, Target_lon);
}

double Calc_Error(double Target_Yaw, double Current_Yaw) {
  Yaw_Error = Target_Yaw - Current_Yaw;
  // 正しく値を更新
  if (Yaw_Error > 180) {
    Yaw_Error -= 360;
  } else if (Yaw_Error < -180) {
    Yaw_Error += 360;
  }
  return Yaw_Error;
}

bool Mode_Select(double Yaw_Error) {
  if (abs(Yaw_Error) >= error_base) {
    return true;  // ズレが大きいのでPivot（超信地旋回）
  } else {
    return false;  // ズレが小さいのでPID（前進しながら微調整）
  }
}

void Pivot(double Yaw_Error) {
  // ループで止めず、一瞬だけモーターを回してすぐ抜ける（全体ループで再計測させる）
  if (Yaw_Error > 0) {
    // 右回り
    digitalWrite(LM_IN1, HIGH);
    digitalWrite(LM_IN2, LOW);
    digitalWrite(RM_IN1, LOW);  // 逆回転させる場合はHIGH/LOW調整
    digitalWrite(RM_IN2, HIGH);
  } else {
    // 左回り
    digitalWrite(LM_IN1, LOW);
    digitalWrite(LM_IN2, HIGH);
    digitalWrite(RM_IN1, HIGH);
    digitalWrite(RM_IN2, LOW);
  }
}

void Pid(double Yaw_Error) {
  // 比例ゲイン(P制御)
  int pwm_val = abs(Yaw_Error) * 10;     // 数値は実際の動きを見て調整
  pwm_val = constrain(pwm_val, 0, 100);  // PWM上限を超えないよう制限

  // 基準速度（前進ベース）
  int base_speed = 150;
  int right_speed = base_speed;
  int left_speed = base_speed;

  // 誤差に応じた速度差分を計算
  if (Yaw_Error > 0) {
    right_speed -= pwm_val;  // 右を遅くして右へ曲がる
    left_speed += pwm_val;
  } else {
    right_speed += pwm_val;
    left_speed -= pwm_val;  // 左を遅くして左へ曲がる
  }

  // 最終的なPWM値の制約
  right_speed = constrain(right_speed, 0, 255);
  left_speed = constrain(left_speed, 0, 255);

  analogWrite(RM_IN1, right_speed);
  analogWrite(RM_IN2, 0);
  analogWrite(LM_IN1, left_speed);
  analogWrite(LM_IN2, 0);
}

void Display(double something) {
  Serial.println(something, 6);
}

// BNO055のキャリブレーション
void Set_Calibration() {
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  // (必要に応じてキャリブレーション走行処理)
}