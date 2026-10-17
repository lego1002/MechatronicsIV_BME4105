// 接腳與參數設定。改接線只需要改這個檔案。
#pragma once
#include <Arduino.h>

#if defined(ARDUINO_ARCH_AVR)
// ================= Arduino Nano（ATmega328P）=================
// ---- MPU6050 ----  I2C 固定在 A4 = SDA, A5 = SCL
#define IMU_ADDR      0x68
#define I2C_HZ        100000  // 杜邦線＋弱上拉，400kHz 不穩
#define IMU_YAW_SIGN  (+1)    // 晶片正面朝上：逆時針（左轉）為正。裝反了改成 -1

// ---- TB6612FNG ----（洞洞板實際焊線）
// PWMA = D6 是 Timer0 硬體 PWM（976Hz，和 millis() 共用計時器，不能改頻率）
// PWMB = D7 沒有硬體 PWM，用 Timer1 中斷模擬（同樣 976Hz），所以 D9/D10 不能再拿來當 PWM
#define PIN_PWMA  6    // 左馬達
#define PIN_PWMB  7    // 右馬達
#define PIN_AIN1  A3
#define PIN_AIN2  A2
#define PIN_BIN1  A1
#define PIN_BIN2  A0
#define PIN_STBY  -1   // STBY 直接接 3.3V（常駐致能，停車靠 PWM=0）
// D2~D5、D8~D12 保留給超音波／蜂鳴器／吸塵器（A6/A7 只能類比輸入）

#else
// ================= STM32 Nucleo-L432KC =================
// ---- MPU6050（I2C1）----
// D0 = PA10 = SDA, D1 = PA9 = SCL（I2C1 的另一組腳位）
// ⚠ D4/D5 經 SB16/SB18 和 A4/A5（BIN2/BIN1）短路，D4/D5 什麼都不要接
#define PIN_IMU_SDA   PA10
#define PIN_IMU_SCL   PA9
#define IMU_ADDR      0x68
#define I2C_HZ        100000  // 杜邦線＋弱上拉，400kHz 不穩
#define IMU_YAW_SIGN  (+1)    // 晶片正面朝上：逆時針（左轉）為正。裝反了改成 -1

// ---- TB6612FNG ----
#define PIN_PWMA  PB4   // D12  左馬達
#define PIN_PWMB  PB5   // D11  右馬達
#define PIN_AIN2  PA3   // A2
#define PIN_AIN1  PA4   // A3
#define PIN_BIN2  PA5   // A4  （經 SB16/SB18 和 D4 短路，D4 保持空接）
#define PIN_BIN1  PA6   // A5  （經 SB16/SB18 和 D5 短路，D5 保持空接）
// STBY 直接接 Nucleo 的 3.3V（常駐致能，停車靠 PWM=0）
#define PIN_STBY  -1   // -1 = 不使用

#define MOTOR_PWM_HZ     20000  // 20kHz，聽不到馬達嘯叫
#endif

#define CMD_TIMEOUT_MS   300    // 超過這麼久沒收到指令就停車
#define MOTOR_RAMP_MS    300    // 加減速限制：PWM 從 0 到 255 至少花這麼久（ms），0 = 不限制。可用 A 指令即時調整
#define IMU_MAX_ERRORS   10     // 連續讀取失敗這麼多次才當作 IMU 斷線（偶發的 I2C 雜訊直接跳過那一筆）

// ---- 其他 ----
#define SERIAL_BAUD      115200
#define IMU_PERIOD_US    5000   // 200Hz 讀陀螺儀
#define REPORT_PERIOD_MS 20     // 50Hz 回報給 Pi
#define CALIB_MS         2000   // 開機／歸零時靜止估計偏移的時間
