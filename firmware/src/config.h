// 接腳與參數設定。改接線只需要改這個檔案。
#pragma once
#include <Arduino.h>

#if defined(ARDUINO_ARCH_AVR)
// ================= Arduino Nano（ATmega328P）=================
// ---- MPU6050 ----  I2C 固定在 A4 = SDA, A5 = SCL
#define IMU_ADDR      0x68
#define I2C_HZ        100000  // 杜邦線＋弱上拉，400kHz 不穩
#define IMU_YAW_SIGN  (+1)    // 晶片正面朝上：逆時針（左轉）為正。裝反了改成 -1

// ---- TB6612FNG ----
// PWMA/PWMB 用 D9/D10（Timer1，31kHz，聽不到馬達嘯叫；不影響 millis()）
#define PIN_PWMA  9    // 左馬達
#define PIN_PWMB  10   // 右馬達
#define PIN_AIN1  7
#define PIN_AIN2  8
#define PIN_BIN1  4
#define PIN_BIN2  5
#define PIN_STBY  A0   // -1 = 不使用（STBY 直接接 VCC）
// D2/D3（外部中斷）、D11/D12、A1~A3 保留給超音波／蜂鳴器／吸塵器

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

// ---- 其他 ----
#define SERIAL_BAUD      115200
#define IMU_PERIOD_US    5000   // 200Hz 讀陀螺儀
#define REPORT_PERIOD_MS 20     // 50Hz 回報給 Pi
#define CALIB_MS         2000   // 開機／歸零時靜止估計偏移的時間
