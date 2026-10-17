# firmware：吸塵機器人韌體（Arduino Nano）

PlatformIO + Arduino 框架。Pi 上不用裝工具鏈，編譯在 Docker 裡進行（`docker/pio`）。
預設板子是 **Arduino Nano（ATmega328P，FT232R，Optiboot @115200）**；舊的 Nucleo-L432KC 還是能編譯（`-e nucleo_l432kc`）。

```bash
./build.sh             # 編譯 Nano（第一次要下載工具鏈）
./flash.sh             # 編譯 + 用 avrdude 經 USB 燒錄到 Nano（/dev/ttyUSB0）
./monitor.sh           # 互動看序列埠，可以直接打指令（Ctrl+] 離開）
./monitor.sh 5         # 只讀 5 秒
./test.sh              # 協定自動測試：開機訊息、50Hz 回報、ping、I2C 掃描、逾時停車、重開機
./imu_motor_test.sh    # 馬達對 IMU 的影響（輪子要懸空）：比較有／沒有加減速限制時的角速度雜訊、漂移、I2C 錯誤
                       # 可加速度參數 ./imu_motor_test.sh 200（0-255，預設 120）

./build.sh -e nucleo_l432kc   # 編譯 Nucleo
./flash.sh nucleo             # 燒錄 Nucleo（複製到 NODE_L432KC 隨身碟）
```

燒錄前要先 `docker stop vacuum-ros2`（序列埠同時只能一個程式用）。
接腳都在 [`src/config.h`](src/config.h)，改接線只要改這個檔案（Nano 和 Nucleo 各一段）。

## 功能

- MPU6050 陀螺儀 z 軸 @200Hz 積分出航向角；開機靜止 2 秒估計偏移，靜止時持續追蹤偏移
- TB6612FNG 雙馬達差速驅動（Nano：PWMA = D6 硬體 PWM，PWMB = D7 用 Timer1 中斷模擬，都是 976Hz）
- 安全機制：300ms 沒收到指令就停車
- 加減速限制（預設 0→255 花 300ms，`A` 指令可調）：避免突然全速／正反轉抽大電流干擾 IMU
- IMU 偶發 I²C 錯誤直接跳過那一筆，連續 10 次失敗才當作斷線；重連後沿用原本的偏移和航向，不會在車子移動時重新校正
- I2C 有逾時（線鬆掉不會卡死），IMU 斷線會每秒自動重試

## 接線（Arduino Nano）

| 元件 | 腳位 | 備註 |
|---|---|---|
| MPU6050 SDA / SCL | A4 / A5 | VCC 接 5V（GY-521 模組上有穩壓） |
| TB6612 PWMA / PWMB | D6 / D7 | 左 / 右馬達。D6 = Timer0 硬體 PWM；D7 沒有硬體 PWM，用 Timer1 中斷模擬（都是 976Hz） |
| TB6612 AIN1 / AIN2 | A3 / A2 | |
| TB6612 BIN1 / BIN2 | A1 / A0 | |
| TB6612 STBY | 3.3V | 常駐致能，停車靠 PWM=0 |
| TB6612 VCC | 5V | VM 接馬達電池，GND 要和 Nano 共地 |
| 板載 LED | D13 | 校正中快閃、正常慢閃、IMU 錯誤恆亮 |
| 保留 | D2~D5、D8~D12 | 超音波、蜂鳴器、吸塵器（D9/D10 被 Timer1 佔用，不能當 PWM） |

注意：Pi 每次打開序列埠（例如啟動 bridge）Nano 都會自動重開，約 1.5 秒後才開始送資料。

Nucleo-L432KC 的接線和已知問題（A4/A5 錫橋、A7 是 VCP TX）見 git 歷史或 `src/config.h` 的 Nucleo 段落。

## 序列埠協定（Nano：/dev/ttyUSB0，Nucleo：/dev/ttyACM0；115200 baud）

Pi → MCU：
```
V <線速度 -255~255> <角速度 -255~255>\n   移動（角速度正 = 左轉），要持續送，300ms 沒收到就停
S\n                                       停車（立刻放掉，不經過加減速）
A <ms>\n                                  加減速限制：PWM 0→255 至少花 ms 毫秒，0 = 不限制（預設 300）
Z\n                                       航向角歸零並重新估計偏移（車子要靜止 2 秒）
P <任意字串>\n                            原樣回傳，用來量延遲
I\n                                       掃描 I²C（除錯用）
R\n                                       重新開機（看開機訊息用）
B <0|1>\n / F <0|1>\n                     蜂鳴器／吸塵器（還沒接）
```

MCU → Pi（約 50Hz）：
```
D <yaw 度> <角速度 度/秒> <前方 cm> <右側 cm> <狀態>\n
    yaw：-180~180，逆時針為正；超音波還沒接，回傳 -1
    狀態：0 正常 / 1 IMU 校正中 / 2 IMU 錯誤
L <訊息>\n                                debug 訊息（例如 `L imu read errors N` 每秒回報一次讀取失敗次數、`L imu lost` 斷線）
```

不用 ROS2 也可以直接看序列埠（需要 dialout 權限，或用 Docker）：
```bash
./monitor.sh
```
