// 吸塵機器人韌體：Arduino Nano（ATmega328P）／ STM32 Nucleo-L432KC
//   - MPU6050 陀螺儀 z 軸 @200Hz 積分出航向角（yaw）
//   - TB6612FNG 雙馬達差速驅動
//   - 透過 USB 序列埠和 Pi 溝通（Nano：/dev/ttyUSB0，Nucleo：/dev/ttyACM0），協定見 ../README.md
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
// 看門狗（中斷＋重開模式）：250ms 沒餵先進中斷做記號，再 250ms 還沒餵就重開，總共 500ms。
// 這塊 Nano 的 bootloader 會清掉 MCUSR 又不傳給程式（實測任何重開都讀到同一個值），
// 所以看門狗重開要靠這個記號判斷；.noinit 的變數重開後不會被清掉
static const uint8_t WDT_MARK = 0xA5;
static volatile uint8_t wdtMark __attribute__((section(".noinit")));
ISR(WDT_vect) { wdtMark = WDT_MARK; }

static void wdtStart() {
  cli();
  wdt_reset();
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = _BV(WDIE) | _BV(WDE) | _BV(WDP2);  // 250ms
  sei();
}
#endif

// ---------------- MPU6050 ----------------
static const float GYRO_LSB_PER_DPS = 65.5f;  // ±500 dps

static bool imuOk = false;
static uint32_t imuRetryMs = 0;
static uint32_t lastImuUs = 0;

static float yawDeg = 0;      // 航向角，(-180, 180]
static float rateDps = 0;     // 角速度（已扣偏移）
static float biasRaw = 0;     // 陀螺儀 z 軸偏移（raw）

static bool calibrating = false;
static uint32_t calibStartMs = 0;
static float calibSum = 0;
static uint32_t calibCount = 0;
static bool biasValid = false;     // 校正過一次之後，IMU 斷線重連就沿用舊的偏移和航向，不再重新校正
static uint8_t imuErrRun = 0;      // 連續讀取失敗次數
static uint16_t imuErrCount = 0;   // 累計讀取失敗次數（每秒回報一次）

static bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool imuRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void startCalibration() {
  calibrating = true;
  calibStartMs = millis();
  calibSum = 0;
  calibCount = 0;
  Serial.println("L calib start (keep still)");
}

static bool imuInit() {
  static uint8_t failCount = 0;
  uint8_t who = 0;
  if (!imuRead(0x75, &who, 1)) {
#if defined(ARDUINO_ARCH_AVR)
    if (failCount++ % 10 == 0) Serial.println("L imu not found (check SDA=A4 SCL=A5, power)");
#else
    if (failCount++ % 10 == 0) Serial.println("L imu not found (check SDA=D0 SCL=D1, power)");
#endif
    return false;
  }
  failCount = 0;
  if (who != 0x68) {
    Serial.print("L imu WHO_AM_I=0x");
    Serial.print(who, HEX);
    Serial.println(" (clone? continuing)");
  }
  bool ok = imuWrite(0x6B, 0x01)     // 喚醒，時脈用陀螺儀 PLL
         && imuWrite(0x1A, 0x03)     // DLPF ~42Hz
         && imuWrite(0x19, 0x04)     // 取樣率 1kHz/(1+4) = 200Hz
         && imuWrite(0x1B, 0x08);    // 陀螺儀 ±500 dps
  if (!ok) {
    Serial.println("L imu config failed");
    return false;
  }
  delay(50);
  Serial.println("L imu ok");
  // 馬達雜訊造成的斷線重連時車子可能正在動，這時重新校正會算出錯的偏移，所以只在第一次校正
  if (!biasValid) startCalibration();
  return true;
}

static bool motorsIdle();

static void imuUpdate() {
  uint32_t now = micros();
  if (now - lastImuUs < IMU_PERIOD_US) return;
  float dt = (now - lastImuUs) * 1e-6f;
  lastImuUs = now;

  uint8_t b[2];
  if (!imuRead(0x47, b, 2)) {  // GYRO_ZOUT_H/L
    // 偶發錯誤就跳過這一筆（下一筆的 dt 會涵蓋這段時間），連續失敗才當作斷線
    imuErrCount++;
    if (++imuErrRun >= IMU_MAX_ERRORS) {
      Serial.println("L imu lost");
      imuOk = false;
      imuRetryMs = millis();
      imuErrRun = 0;
    }
    return;
  }
  imuErrRun = 0;
  float raw = (int16_t)((b[0] << 8) | b[1]);

  if (calibrating) {
    calibSum += raw;
    calibCount++;
    rateDps = 0;
    if (millis() - calibStartMs >= CALIB_MS && calibCount > 0) {
      biasRaw = calibSum / calibCount;
      yawDeg = 0;
      calibrating = false;
      biasValid = true;
      Serial.print("L calib done bias=");
      Serial.print(biasRaw / GYRO_LSB_PER_DPS, 3);
      Serial.println(" dps");
    }
    return;
  }

  // 車子靜止時，慢慢追蹤偏移（溫度造成的漂移）
  float err = raw - biasRaw;
  if (motorsIdle() && fabsf(err) < 0.3f * GYRO_LSB_PER_DPS) biasRaw += 0.002f * err;

  rateDps = IMU_YAW_SIGN * (raw - biasRaw) / GYRO_LSB_PER_DPS;
  if (dt > 0.05f) dt = 0.05f;  // 迴圈卡住時避免一次積分太多
  yawDeg += rateDps * dt;
  if (yawDeg > 180) yawDeg -= 360;
  else if (yawDeg <= -180) yawDeg += 360;
}

// ---------------- 馬達 ----------------
static bool motorsAllowed = true;
static int16_t cmdLeft = 0, cmdRight = 0;   // 目標
static int16_t outLeft = 0, outRight = 0;   // 實際輸出（經過加減速限制）
static int32_t outLeftQ = 0, outRightQ = 0; // 同上，單位 1/256 PWM（慢加速時每圈的步進不到 1）
static uint16_t accelMs = MOTOR_ACCEL_MS, decelMs = MOTOR_DECEL_MS;
static uint32_t lastRampUs = 0;
static uint32_t lastCmdMs = 0;
static uint32_t lastNonzeroMs = 0;

static bool motorsIdle() { return millis() - lastNonzeroMs > 1000; }

// ---------------- 直線航向保持 ----------------
// V 指令只記下 cmdLin/cmdAng，由 mixUpdate() 換算成左右輪目標。
// 直走時用陀螺儀航向做 PID，修正兩顆馬達速度不一樣造成的偏移。
static int16_t cmdLin = 0, cmdAng = 0;
static float holdKp = HOLD_KP, holdKi = HOLD_KI, holdKd = HOLD_KD;
static bool holdActive = false;   // 正在鎖航向
static bool holdGaveUp = false;   // 誤差太大放棄，停車（線速度 = 0）後才會再啟用
static float holdYaw = 0, holdI = 0, holdCorr = 0;
static uint32_t lastHoldUs = 0;

static void mixUpdate() {
  bool enabled = holdKp > 0 || holdKi > 0 || holdKd > 0;
  if (cmdLin == 0) holdGaveUp = false;
  bool want = enabled && !holdGaveUp && imuOk && !calibrating && cmdLin != 0 &&
              abs(cmdAng) <= HOLD_ANG_DEADBAND;
  if (!want) {
    holdActive = false;
  } else {
    uint32_t now = micros();
    if (!holdActive) {  // 剛開始直走：鎖住當下的航向
      holdActive = true;
      holdYaw = yawDeg;
      holdI = 0;
      holdCorr = 0;
      lastHoldUs = now;
    } else if (now - lastHoldUs >= IMU_PERIOD_US) {  // 跟 IMU 一樣 200Hz 更新
      float dt = (now - lastHoldUs) * 1e-6f;
      lastHoldUs = now;
      if (dt > 0.05f) dt = 0.05f;
      float err = holdYaw - yawDeg;  // 正 = 車頭偏右了，要往左修
      if (err > 180) err -= 360;
      else if (err < -180) err += 360;
      if (fabsf(err) > HOLD_MAX_ERR_DEG) {
        holdActive = false;
        holdGaveUp = true;
        holdCorr = 0;
        Serial.println(F("L heading hold off: error >45deg (bumped, or IMU_YAW_SIGN wrong)"));
      } else {
        holdI += err * dt;
        if (holdKi > 0) {  // 抗積分飽和：積分項最多佔修正上限的一半
          float lim = HOLD_MAX_CORR / 2 / holdKi;
          holdI = constrain(holdI, -lim, lim);
        }
        holdCorr = holdKp * err + holdKi * holdI - holdKd * rateDps;
        holdCorr = constrain(holdCorr, -HOLD_MAX_CORR, HOLD_MAX_CORR);
      }
    }
  }
  int16_t ang = holdActive ? (int16_t)lroundf(holdCorr) : cmdAng;
  cmdLeft = constrain(cmdLin - ang, -255, 255);
  cmdRight = constrain(cmdLin + ang, -255, 255);
}

#if defined(ARDUINO_ARCH_AVR)
// ---- PWM ----
// 有硬體 PWM 的腳位（D5/D6 = Timer0，976Hz）直接 analogWrite。
// 沒有的（例如 D7）用 Timer1 中斷模擬：8-bit fast PWM、prescaler 64 → 16MHz/64/256 ≈ 976Hz，
// 溢位時拉高、比較值到了拉低。Timer1 只用中斷，不接 OC1A/OC1B（D9/D10）腳位。
#define SOFT_PWM_MIN  4      // 太小的工作週期中斷來不及處理，直接當 0（馬達也轉不動）

static int idleSda = -1, idleScl = -1;
// Nano 沒有 Nucleo 的錫橋問題；這裡只在 Wire.begin() 前關掉內建上拉讀一次 I2C 電位：
// MPU6050 模組有通電的話，板上的上拉電阻會讓兩條線都是 1
static bool solderBridgesShorted() {
  pinMode(SDA, INPUT);
  pinMode(SCL, INPUT);
  delayMicroseconds(50);
  idleSda = digitalRead(SDA);
  idleScl = digitalRead(SCL);
  return false;
}
static bool hasHwPwm(uint8_t pin) { return digitalPinToTimer(pin) != NOT_ON_TIMER; }

static volatile uint8_t *softOut[2] = {nullptr, nullptr};
static uint8_t softMask[2];
static volatile uint8_t softDuty[2] = {0, 0};

ISR(TIMER1_OVF_vect) {
  for (uint8_t i = 0; i < 2; i++)
    if (softOut[i] && softDuty[i]) *softOut[i] |= softMask[i];
}
ISR(TIMER1_COMPA_vect) { if (softOut[0] && softDuty[0] < 255) *softOut[0] &= ~softMask[0]; }
ISR(TIMER1_COMPB_vect) { if (softOut[1] && softDuty[1] < 255) *softOut[1] &= ~softMask[1]; }

static void pwmInit(int idx, uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  if (hasHwPwm(pin)) return;
  softOut[idx] = portOutputRegister(digitalPinToPort(pin));
  softMask[idx] = digitalPinToBitMask(pin);
  TCCR1A = _BV(WGM10);                        // fast PWM 8-bit，OC1A/OC1B 不輸出
  TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);  // prescaler 64
  TIMSK1 |= _BV(TOIE1) | (idx == 0 ? _BV(OCIE1A) : _BV(OCIE1B));
}

static void pwmWrite(int idx, uint8_t pin, uint8_t duty) {
  if (!softOut[idx]) {
    analogWrite(pin, duty);
    return;
  }
  if (duty < SOFT_PWM_MIN) duty = 0;
  softDuty[idx] = duty;
  if (idx == 0) OCR1A = duty;
  else OCR1B = duty;
}

#else
static int idleSda = -1, idleScl = -1;
static bool extPullup = false;          // 有外部上拉 = IMU 模組有通電、線有接上  // I2C 閒置時的電位，正常應該都是 1

// 確認 BIN1/BIN2 沒有和 I2C 短路（例如 IMU 又接回 D4/D5），要在 Wire.begin() 之前檢查
static bool solderBridgesShorted() {
  const uint32_t aPins[] = {PIN_BIN1, PIN_BIN2};
  bool shorted = false;
  // 先用下拉讀一次：MPU6050 模組有通電的話，板上的上拉電阻會讓這裡讀到 1
  pinMode(PIN_IMU_SDA, INPUT_PULLDOWN);
  pinMode(PIN_IMU_SCL, INPUT_PULLDOWN);
  delayMicroseconds(50);
  extPullup = digitalRead(PIN_IMU_SDA) && digitalRead(PIN_IMU_SCL);
  pinMode(PIN_IMU_SDA, INPUT_PULLUP);
  pinMode(PIN_IMU_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  idleSda = digitalRead(PIN_IMU_SDA);
  idleScl = digitalRead(PIN_IMU_SCL);
  for (uint32_t a : aPins) {
    pinMode(a, OUTPUT);
    digitalWrite(a, LOW);
    delayMicroseconds(50);
    // 只看原本是 1、被拉成 0 的線；原本就是 0 的線（沒接好）不算短路
    if ((idleSda && digitalRead(PIN_IMU_SDA) == LOW) || (idleScl && digitalRead(PIN_IMU_SCL) == LOW)) shorted = true;
    pinMode(a, INPUT);
  }
  return shorted;
}

// ---- PWM ----
// L432KC 的 D11/D12 沒有接計時器，不能用 analogWrite。
// 有硬體 PWM 的腳位（例如 D9/D10）用 analogWrite（20kHz）；
// 沒有的就用 TIM2 中斷模擬（SOFT_PWM_HZ），週期開始拉高、比較值到了拉低。
#define SOFT_PWM_HZ   5000
#define SOFT_PWM_MIN  8      // 太小的工作週期中斷來不及處理，直接當 0（馬達也轉不動）

static HardwareTimer *softTim = nullptr;
static PinName softPin[2] = {NC, NC};
static volatile uint8_t softDuty[2] = {0, 0};

static bool hasHwPwm(uint32_t pin) {
  return pin_in_pinmap(digitalPinToPinName(pin), PinMap_TIM);
}

static void softUpdate() {
  for (int i = 0; i < 2; i++)
    if (softPin[i] != NC) digitalWriteFast(softPin[i], softDuty[i] > 0);
}
static void softCompare1() { if (softDuty[0] < 255) digitalWriteFast(softPin[0], LOW); }
static void softCompare2() { if (softDuty[1] < 255) digitalWriteFast(softPin[1], LOW); }

static void pwmInit(int idx, uint32_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  if (hasHwPwm(pin)) return;
  softPin[idx] = digitalPinToPinName(pin);
  if (!softTim) {
    softTim = new HardwareTimer(TIM2);
    softTim->setOverflow(SOFT_PWM_HZ, HERTZ_FORMAT);
    softTim->attachInterrupt(softUpdate);
  }
  uint32_t ch = idx + 1;
  softTim->setMode(ch, TIMER_OUTPUT_DISABLED);  // 只用比較中斷，不接腳位
  softTim->setCaptureCompare(ch, 0, RESOLUTION_8B_COMPARE_FORMAT);
  softTim->attachInterrupt(ch, idx == 0 ? softCompare1 : softCompare2);
  softTim->resume();
}

static void pwmWrite(int idx, uint32_t pin, uint8_t duty) {
  if (softPin[idx] == NC) {
    analogWrite(pin, duty);
    return;
  }
  if (duty < SOFT_PWM_MIN) duty = 0;
  softDuty[idx] = duty;
  softTim->setCaptureCompare(idx + 1, duty, RESOLUTION_8B_COMPARE_FORMAT);
}
#endif

static void setMotor(int idx, uint8_t pwmPin, uint8_t in1, uint8_t in2, int16_t v) {
  v = constrain(v, -255, 255);
  digitalWrite(in1, v > 0 ? HIGH : LOW);
  digitalWrite(in2, v < 0 ? HIGH : LOW);
  pwmWrite(idx, pwmPin, abs(v));
}

static void applyMotors() {
  if (!motorsAllowed) return;
  setMotor(0, PIN_PWMA, PIN_AIN1, PIN_AIN2, MOTOR_SWAP_LR ? outRight : outLeft);
  setMotor(1, PIN_PWMB, PIN_BIN1, PIN_BIN2, MOTOR_SWAP_LR ? outLeft : outRight);
}

// 急停（S、指令逾時）：不經過加減速，直接放掉（TB6612 IN1=IN2=L 是滑行，不會有煞車電流）
static void stopNow() {
  cmdLin = cmdAng = 0;
  holdActive = false;
  cmdLeft = cmdRight = outLeft = outRight = 0;
  outLeftQ = outRightQ = 0;
  applyMotors();
}

// 梯形加減速：離開 0（加速）每秒最多變化 255*1000/accelMs，往 0（減速）用 decelMs；
// 要反轉時先用減速斜率降到 0，再用加速斜率往反方向。輸出用 1/256 PWM 的定點數累積，慢斜率也不會被吃掉。
// 突然全速或正反轉會抽大電流，電池電壓掉下來（Pi 會重開）、雜訊變大，IMU 讀值也會受影響
static int32_t rampWheel(int32_t q, int16_t target, uint32_t el) {
  int32_t t = (int32_t)target << 8;
  if (q == t) return q;
  bool slowing = (q > 0 && t < q) || (q < 0 && t > q);
  if (slowing && ((q > 0 && t < 0) || (q < 0 && t > 0))) t = 0;  // 反轉：先停到 0
  uint16_t ms = slowing ? decelMs : accelMs;
  uint32_t step = ms == 0 ? (510UL << 8) : el * (255UL * 256UL / 4) / (ms * 250UL);  // = el*255*256/(ms*1000)，避免溢位
  if (t > q) return t - q > (int32_t)step ? q + (int32_t)step : t;
  return q - t > (int32_t)step ? q - (int32_t)step : t;
}

static void rampUpdate() {
  uint32_t now = micros();
  uint32_t el = now - lastRampUs;
  lastRampUs = now;
  if (outLeft == cmdLeft && outRight == cmdRight) return;
  if (el > 50000) el = 50000;  // 迴圈卡住過久也不要一口氣跳太多
  outLeftQ = rampWheel(outLeftQ, cmdLeft, el);
  outRightQ = rampWheel(outRightQ, cmdRight, el);
  outLeft = outLeftQ / 256;   // 往 0 截斷，到目標時剛好整除
  outRight = outRightQ / 256;
  applyMotors();
}

static void motorsInit(bool bridgesShorted) {
  if (bridgesShorted) {
    motorsAllowed = false;
    // BIN1/BIN2 保持輸入，才不會干擾 I2C
    return;
  }
#if !defined(ARDUINO_ARCH_AVR)
  analogWriteResolution(8);
  analogWriteFrequency(MOTOR_PWM_HZ);
#endif
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pwmInit(0, PIN_PWMA);
  pwmInit(1, PIN_PWMB);
  if (PIN_STBY >= 0) {
    pinMode(PIN_STBY, OUTPUT);
    digitalWrite(PIN_STBY, HIGH);
  }
  applyMotors();
}

static void reportMotorWarning() {
  Serial.println("L motors disabled: BIN1/BIN2 shorted to I2C (IMU must be on D0/D1, keep D4/D5 unconnected)");
}

// ---------------- 吸塵器（繼電器）----------------
static bool vacuumOn = false;
static uint32_t lastRxMs = 0;

static void setVacuum(bool on) {
  vacuumOn = on;
  if (PIN_VACUUM >= 0) digitalWrite(PIN_VACUUM, on ? VACUUM_ON_LEVEL : !VACUUM_ON_LEVEL);
}

static void vacuumInit() {
  if (PIN_VACUUM < 0) return;
  // 先設好關閉電位再切成輸出，低電位觸發的模組才不會在開機時吸合一下
  setVacuum(false);
  pinMode(PIN_VACUUM, OUTPUT);
}

// ---------------- 序列埠指令 ----------------
static char line[48];
static uint8_t lineLen = 0;

static void handleLine(char *s) {
  int a = 0, b = 0;
  switch (s[0]) {
    case 'V':  // V <線速度> <角速度>，-255~255；角速度正 = 左轉
      if (sscanf(s + 1, "%d %d", &a, &b) == 2) {
        cmdLin = constrain(a, -255, 255);  // 左右輪目標由 mixUpdate() 算（含航向保持修正）
        cmdAng = constrain(b, -255, 255);
        lastCmdMs = millis();
        if (cmdLin || cmdAng) lastNonzeroMs = lastCmdMs;
        if (!motorsAllowed && (cmdLin || cmdAng)) {
          static uint32_t lastWarn = 0;
          if (millis() - lastWarn > 2000) { lastWarn = millis(); reportMotorWarning(); }
        }
      }
      break;
    case 'S':
      stopNow();
      break;
    case 'A':  // A <加速ms> [減速ms]：PWM 0→255 花的時間，0 = 不限制；只給一個數 = 加減速都設成它
      {
        int n = sscanf(s + 1, "%d %d", &a, &b);
        if (n >= 1) {
          accelMs = constrain(a, 0, 5000);
          decelMs = n == 2 ? constrain(b, 0, 5000) : accelMs;
        }
        Serial.print(F("L ramp accel "));
        Serial.print(accelMs);
        Serial.print(F(" decel "));
        Serial.print(decelMs);
        Serial.println(F(" ms"));
      }
      break;
    case 'G':  // G <kp*100> <ki*100> <kd*100>：直線航向保持增益，全 0 = 關閉（AVR 的 sscanf 不支援 %f）
      {
        int c = 0;
        if (sscanf(s + 1, "%d %d %d", &a, &b, &c) == 3) {
          holdKp = max(a, 0) / 100.0f;
          holdKi = max(b, 0) / 100.0f;
          holdKd = max(c, 0) / 100.0f;
          holdActive = false;
        }
        Serial.print(F("L hold kp="));
        Serial.print(holdKp, 2);
        Serial.print(F(" ki="));
        Serial.print(holdKi, 2);
        Serial.print(F(" kd="));
        Serial.println(holdKd, 2);
      }
      break;
    case 'Z':  // 航向角歸零並重新估計偏移（車子要靜止）
      if (imuOk) startCalibration();
      break;
    case 'P':  // P <n>：原樣回傳，用來量延遲
      Serial.println(s);
      break;
    case 'R':  // 重新開機（看開機訊息用）
#if defined(ARDUINO_ARCH_AVR)
      wdt_enable(WDTO_15MS);  // Optiboot 會在看門狗重開後清掉 WDT，不會卡住
      for (;;) {}
#else
      NVIC_SystemReset();
#endif
      break;
#if defined(ARDUINO_ARCH_AVR)
    case 'H':  // 測試用：故意卡死。H 0 = 等看門狗重開；H 1 = 關掉看門狗，等 Pi 用 DTR 重置
      if (sscanf(s + 1, "%d", &a) == 1) {
        stopNow();
        setVacuum(false);
        if (a == 1) wdt_disable();
        Serial.println(F("L test hang"));
        Serial.flush();
        for (;;) {}
      }
      break;
#endif
    case 'I':  // 掃描 I2C 匯流排，查 IMU 接線用
      Serial.print("L i2c scan:");
      for (uint8_t addr = 1; addr < 127; addr++) {
#if defined(ARDUINO_ARCH_AVR)
        wdt_reset();  // 每個位址最多等 3ms 逾時，全部掃完可能接近看門狗時間
#endif
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.print(" 0x");
          Serial.print(addr, HEX);
        }
      }
      Serial.println(" (MPU6050 = 0x68/0x69)");
      {
        // 閒置時 SDA/SCL 應該一直是高：取樣 1000 次數低電位次數。
        // 一直低 = 有東西把線拉住（IMU 卡住／沒電）；偶爾低 = 雜訊打進來
        // 第二輪先 Wire.end() 放開 MCU 自己的 I2C 硬體：如果線還是低，就是外面（IMU 或接線）拉住的
        for (uint8_t pass = 0; pass < 2; pass++) {
          if (pass == 1) Wire.end();
          uint16_t sdaLow = 0, sclLow = 0;
          for (uint16_t i = 0; i < 1000; i++) {
            if (!digitalRead(SDA)) sdaLow++;
            if (!digitalRead(SCL)) sclLow++;
            delayMicroseconds(10);
          }
          Serial.print(pass ? F("L i2c released, low SDA ") : F("L i2c idle low SDA "));
          Serial.print(sdaLow);
          Serial.print(F("/1000 SCL "));
          Serial.print(sclLow);
          Serial.println(F("/1000"));
        }
        Wire.begin();
#if defined(ARDUINO_ARCH_AVR)
        Wire.setWireTimeout(3000, true);
#endif
      }
      break;
    case 'B':
      Serial.println("L buzzer not wired yet");
      break;
    case 'F':  // F <0|1>：吸塵器開關
      if (sscanf(s + 1, "%d", &a) == 1) {
        if (PIN_VACUUM < 0) {
          Serial.println("L vacuum not wired");
          break;
        }
        setVacuum(a != 0);
        Serial.println(vacuumOn ? "L vacuum on" : "L vacuum off");
      }
      break;
    default:
      Serial.print("L unknown cmd: ");
      Serial.println(s);
  }
}

static void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = 0;
      if (lineLen) {
        lastRxMs = millis();
        handleLine(line);
      }
      lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---------------- 主程式 ----------------
void setup() {
#if defined(ARDUINO_ARCH_AVR)
  bool wdtReset = wdtMark == WDT_MARK;
  wdtMark = 0;
  MCUSR = 0;
  wdt_disable();
#endif
  vacuumInit();
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_BUILTIN, OUTPUT);

  bool shorted = solderBridgesShorted();
  motorsInit(shorted);

#if !defined(ARDUINO_ARCH_AVR)
  Wire.setSDA(PIN_IMU_SDA);
  Wire.setSCL(PIN_IMU_SCL);
#endif
  Wire.begin();
  Wire.setClock(I2C_HZ);
#if defined(ARDUINO_ARCH_AVR)
  Wire.setWireTimeout(3000, true);  // AVR 的 Wire 預設沒有逾時，線鬆掉會卡死整個迴圈
#endif

  delay(100);
  Serial.println("L boot vacuum-fw");
#if defined(ARDUINO_ARCH_AVR)
  Serial.println(wdtReset ? F("L reset cause: watchdog") : F("L reset cause: other"));
  Serial.print(F("L i2c idle SDA="));
  Serial.print(idleSda);
  Serial.print(F(" SCL="));
  Serial.print(idleScl);
  Serial.println(idleSda && idleScl ? F(" ext-pullup=yes") : F(" ext-pullup=NO (IMU module unpowered or not connected?)"));
#else
  Serial.print("L i2c idle SDA=");
  Serial.print(idleSda);
  Serial.print(" SCL=");
  Serial.print(idleScl);
  Serial.println(extPullup ? " ext-pullup=yes" : " ext-pullup=NO (IMU module unpowered or not connected?)");
  if (shorted) reportMotorWarning();
#endif
  Serial.print("L pwm A=");
  Serial.print(hasHwPwm(PIN_PWMA) ? "hw" : "soft");
  Serial.print(" B=");
  Serial.println(hasHwPwm(PIN_PWMB) ? "hw" : "soft");
  if (PIN_STBY < 0) Serial.println("L STBY pin not set: tie TB6612 STBY to 3.3V or set PIN_STBY");
  imuOk = imuInit();
  imuRetryMs = millis();
  lastImuUs = micros();
#if defined(ARDUINO_ARCH_AVR)
  wdtStart();  // setup 跑完才開，開機流程不會被誤判成當機
#endif
}

void loop() {
#if defined(ARDUINO_ARCH_AVR)
  wdt_reset();
  if (wdtMark == WDT_MARK) {  // 卡超過 250ms 但又恢復了：不算當機，清掉記號並重新啟用中斷（硬體進中斷時會關掉）
    wdtMark = 0;
    wdtStart();
  }
#endif
  readSerial();

  if (imuOk) {
    imuUpdate();
  } else if (millis() - imuRetryMs > 1000) {
    imuRetryMs = millis();
    // 重新初始化 I2C：線拔掉又接回時，I2C 周邊可能卡在錯誤狀態
    Wire.end();
    Wire.begin();
    Wire.setClock(I2C_HZ);
#if defined(ARDUINO_ARCH_AVR)
    Wire.setWireTimeout(3000, true);
#endif
    imuOk = imuInit();
    lastImuUs = micros();
  }

  // 安全機制：太久沒收到指令就停車
  if ((cmdLin || cmdAng || outLeft || outRight) && millis() - lastCmdMs > CMD_TIMEOUT_MS) {
    stopNow();
    Serial.println("L cmd timeout, stop");
  }
  mixUpdate();
  rampUpdate();

  // 安全機制：Pi 沒有任何指令進來就關吸塵器
  if (vacuumOn && millis() - lastRxMs > VACUUM_TIMEOUT_MS) {
    setVacuum(false);
    Serial.println("L cmd timeout, vacuum off");
  }

  static uint32_t lastErrReport = 0;
  if (imuErrCount && millis() - lastErrReport >= 1000) {
    lastErrReport = millis();
    Serial.print("L imu read errors ");
    Serial.println(imuErrCount);
    imuErrCount = 0;
  }

  static uint32_t lastReport = 0;
  if (millis() - lastReport >= REPORT_PERIOD_MS) {
    lastReport = millis();
    // D <yaw 度> <角速度 度/秒> <前方 cm> <右側 cm> <狀態>
    // 狀態：0 正常 / 1 校正中 / 2 IMU 錯誤；超音波還沒接，先回傳 -1
    Serial.print("D ");
    Serial.print(yawDeg, 2);
    Serial.print(' ');
    Serial.print(rateDps, 2);
    Serial.print(" -1 -1 ");
    Serial.println(!imuOk ? 2 : calibrating ? 1 : 0);
  }

  // LED：校正中快閃、正常慢閃、IMU 錯誤恆亮
  uint32_t period = !imuOk ? 0 : calibrating ? 100 : 1000;
  digitalWrite(LED_BUILTIN, period == 0 ? HIGH : (millis() / period) % 2);
}
