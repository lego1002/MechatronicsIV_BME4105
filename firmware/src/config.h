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
#define PIN_PWMA  6    // 右馬達（A 通道）
#define PIN_PWMB  7    // 左馬達（B 通道）
#define MOTOR_SWAP_LR  1  // 1 = A 通道接右馬達、B 通道接左馬達
#define PIN_AIN1  A3
#define PIN_AIN2  A2
#define PIN_BIN1  A0   // 2026-11-12 左輪方向相反，BIN1/BIN2 對調（原本 A1/A0）
#define PIN_BIN2  A1
#define PIN_STBY  -1   // STBY 直接接 3.3V（常駐致能，停車靠 PWM=0）
// ---- 吸塵器繼電器（JQC-3FF-S-Z，馬達接 COM/NO）----
#define PIN_VACUUM       8
#define VACUUM_ON_LEVEL  LOW   // 實測 D8 LOW 吸塵器才會轉（2026-11-06）；如果改成高電位觸發，這裡改成 HIGH
// D2~D5、D9~D12 保留給超音波／蜂鳴器（A6/A7 只能類比輸入）

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
#define MOTOR_SWAP_LR  0
#define PIN_AIN2  PA3   // A2
#define PIN_AIN1  PA4   // A3
#define PIN_BIN2  PA5   // A4  （經 SB16/SB18 和 D4 短路，D4 保持空接）
#define PIN_BIN1  PA6   // A5  （經 SB16/SB18 和 D5 短路，D5 保持空接）
// STBY 直接接 Nucleo 的 3.3V（常駐致能，停車靠 PWM=0）
#define PIN_STBY  -1   // -1 = 不使用

#define MOTOR_PWM_HZ     20000  // 20kHz，聽不到馬達嘯叫

#define PIN_VACUUM       -1     // Nucleo 沒接吸塵器
#define VACUUM_ON_LEVEL  HIGH
#endif

#define CMD_TIMEOUT_MS   300    // 超過這麼久沒收到指令就停車
#define VACUUM_TIMEOUT_MS 2000  // 超過這麼久沒收到任何指令就關吸塵器（Pi 當掉或 USB 斷線）
// 梯形加減速：PWM 從 0 到 255 至少花這麼久（ms），0 = 不限制。可用 A <加速> <減速> 即時調整（網頁也能調）
#define MOTOR_ACCEL_MS   800    // 加速（離開 0）：慢一點，起步電流小、電池電壓不會被拉垮
#define MOTOR_DECEL_MS   300    // 減速（往 0）／反轉時先減到 0：快一點，放開按鍵就停
#define IMU_MAX_ERRORS   10     // 連續讀取失敗這麼多次才當作 IMU 斷線（偶發的 I2C 雜訊直接跳過那一筆）

// ---- 直線航向保持（IMU 矯正左右輪速度差）----
// 直走（|角速度| <= HOLD_ANG_DEADBAND 且線速度不為 0）時鎖住當下航向，用 PID 修正角速度。
// 單位：輸出是 PWM（0~255），誤差是度。可用 G 指令即時調整（G <kp*100> <ki*100> <kd*100>，G 0 0 0 = 關閉）
#define HOLD_KP           5.0f   // PWM / 度
#define HOLD_KI           3.0f   // PWM / (度·秒)，消除兩顆馬達固定的速度差
#define HOLD_KD           0.5f   // PWM / (度/秒)，阻尼
#define HOLD_MAX_CORR     80     // 修正量上限（PWM）
#define HOLD_ANG_DEADBAND 10     // 角速度指令小於這個值當作直走（搖桿不會剛好回到 0）
#define HOLD_MAX_ERR_DEG  45     // 誤差超過這麼多度就放棄（被撞歪，或 IMU_YAW_SIGN 方向設反），直到停車再重來

// ---- 其他 ----
#define SERIAL_BAUD      115200
#define IMU_PERIOD_US    5000   // 200Hz 讀陀螺儀
#define REPORT_PERIOD_MS 20     // 50Hz 回報給 Pi
#define CALIB_MS         2000   // 開機／歸零時靜止估計偏移的時間
