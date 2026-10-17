// 吸塵機器人韌體：Arduino Nano（ATmega328P）／ STM32 Nucleo-L432KC
//   - MPU6050 陀螺儀 z 軸 @200Hz 積分出航向角（yaw）
//   - TB6612FNG 雙馬達差速驅動
//   - 透過 USB 序列埠和 Pi 溝通（Nano：/dev/ttyUSB0，Nucleo：/dev/ttyACM0），協定見 ../README.md
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
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
static uint16_t rampMs = MOTOR_RAMP_MS;
static uint32_t lastRampUs = 0;
static uint32_t lastCmdMs = 0;
static uint32_t lastNonzeroMs = 0;

static bool motorsIdle() { return millis() - lastNonzeroMs > 1000; }

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
  setMotor(0, PIN_PWMA, PIN_AIN1, PIN_AIN2, outLeft);
  setMotor(1, PIN_PWMB, PIN_BIN1, PIN_BIN2, outRight);
}

// 急停（S、指令逾時）：不經過加減速，直接放掉（TB6612 IN1=IN2=L 是滑行，不會有煞車電流）
static void stopNow() {
  cmdLeft = cmdRight = outLeft = outRight = 0;
  applyMotors();
}

// 加減速限制：輸出每次最多往目標靠近 255 * 經過時間 / rampMs。
// 突然全速或正反轉會抽大電流，電壓掉下來、雜訊變大，IMU 讀值會受影響
static int16_t rampToward(int16_t out, int16_t target, int16_t step) {
  if (target > out) return target - out > step ? out + step : target;
  return out - target > step ? out - step : target;
}

static void rampUpdate() {
  uint32_t now = micros();
  uint32_t el = now - lastRampUs;
  if (outLeft == cmdLeft && outRight == cmdRight) {
    lastRampUs = now;
    return;
  }
  uint32_t step = rampMs == 0 ? 510 : el * 255UL / (rampMs * 1000UL);
  if (step > 510) step = 510;
  if (step == 0) return;  // 還不到 1 格，等累積
  lastRampUs = now;
  outLeft = rampToward(outLeft, cmdLeft, step);
  outRight = rampToward(outRight, cmdRight, step);
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

// ---------------- 序列埠指令 ----------------
static char line[48];
static uint8_t lineLen = 0;

static void handleLine(char *s) {
  int a = 0, b = 0;
  switch (s[0]) {
    case 'V':  // V <線速度> <角速度>，-255~255；角速度正 = 左轉
      if (sscanf(s + 1, "%d %d", &a, &b) == 2) {
        a = constrain(a, -255, 255);
        b = constrain(b, -255, 255);
        cmdLeft = constrain(a - b, -255, 255);
        cmdRight = constrain(a + b, -255, 255);
        lastCmdMs = millis();
        if (cmdLeft || cmdRight) lastNonzeroMs = lastCmdMs;
        if (!motorsAllowed && (cmdLeft || cmdRight)) {
          static uint32_t lastWarn = 0;
          if (millis() - lastWarn > 2000) { lastWarn = millis(); reportMotorWarning(); }
        }
      }
      break;
    case 'S':
      stopNow();
      break;
    case 'A':  // A <ms>：加減速限制，PWM 0→255 花的時間，0 = 不限制
      if (sscanf(s + 1, "%d", &a) == 1) {
        rampMs = constrain(a, 0, 5000);
        Serial.print("L ramp ");
        Serial.print(rampMs);
        Serial.println(" ms");
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
    case 'I':  // 掃描 I2C 匯流排，查 IMU 接線用
      Serial.print("L i2c scan:");
      for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.print(" 0x");
          Serial.print(addr, HEX);
        }
      }
      Serial.println(" (MPU6050 = 0x68/0x69)");
      break;
    case 'B':
    case 'F':
      Serial.println("L buzzer/vacuum not wired yet");
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
      if (lineLen) handleLine(line);
      lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---------------- 主程式 ----------------
void setup() {
#if defined(ARDUINO_ARCH_AVR)
  MCUSR = 0;
  wdt_disable();
#endif
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
}

void loop() {
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
  if ((cmdLeft || cmdRight || outLeft || outRight) && millis() - lastCmdMs > CMD_TIMEOUT_MS) {
    stopNow();
    Serial.println("L cmd timeout, stop");
  }
  rampUpdate();

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
