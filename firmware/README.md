# firmware：吸塵機器人韌體（Arduino Nano）

PlatformIO + Arduino 框架。Pi 上不用裝工具鏈，編譯在 Docker 裡進行（`docker/pio`）。
預設板子是 **Arduino Nano（ATmega328P，FT232R，Optiboot @115200）**；舊的 Nucleo-L432KC 還是能編譯（`-e nucleo_l432kc`）。

```bash
./build.sh             # 編譯 Nano（第一次要下載工具鏈）
./flash.sh             # 編譯 + 用 avrdude 經 USB 燒錄到 Nano（/dev/ttyUSB0）
./monitor.sh           # 互動看序列埠，可以直接打指令（Ctrl+] 離開）
./monitor.sh 5         # 只讀 5 秒
./test.sh              # 協定自動測試：開機訊息、50Hz 回報、ping、I2C 掃描、逾時停車、重開機

./build.sh -e nucleo_l432kc   # 編譯 Nucleo
./flash.sh nucleo             # 燒錄 Nucleo（複製到 NODE_L432KC 隨身碟）
```

燒錄前要先 `docker stop vacuum-ros2`（序列埠同時只能一個程式用）。
接腳都在 [`src/config.h`](src/config.h)，改接線只要改這個檔案（Nano 和 Nucleo 各一段）。

## 功能

- MPU6050 陀螺儀 z 軸 @200Hz 積分出航向角；開機靜止 2 秒估計偏移，靜止時持續追蹤偏移
- TB6612FNG 雙馬達差速驅動（Nano：D9/D10 Timer1 硬體 PWM，31kHz）
- 安全機制：300ms 沒收到指令就停車
- I2C 有逾時（線鬆掉不會卡死），IMU 斷線會每秒自動重試

## 接線（Arduino Nano）

| 元件 | 腳位 | 備註 |
|---|---|---|
| MPU6050 SDA / SCL | A4 / A5 | VCC 接 5V（GY-521 模組上有穩壓） |
| TB6612 PWMA / PWMB | D9 / D10 | 左 / 右馬達，Timer1 31kHz |
| TB6612 AIN1 / AIN2 | D7 / D8 | |
| TB6612 BIN1 / BIN2 | D4 / D5 | |
| TB6612 STBY | A0 | 開機時韌體才拉高，燒錄／重開期間馬達不會亂轉 |
| TB6612 VCC | 5V | VM 接馬達電池，GND 要和 Nano 共地 |
| 板載 LED | D13 | 校正中快閃、正常慢閃、IMU 錯誤恆亮 |
| 保留 | D2/D3、D11/D12、A1~A3 | 超音波、蜂鳴器、吸塵器 |

注意：Pi 每次打開序列埠（例如啟動 bridge）Nano 都會自動重開，約 1.5 秒後才開始送資料。

Nucleo-L432KC 的接線和已知問題（A4/A5 錫橋、A7 是 VCP TX）見 git 歷史或 `src/config.h` 的 Nucleo 段落。

## 序列埠協定（Nano：/dev/ttyUSB0，Nucleo：/dev/ttyACM0；115200 baud）

Pi → MCU：
```
V <線速度 -255~255> <角速度 -255~255>\n   移動（角速度正 = 左轉），要持續送，300ms 沒收到就停
S\n                                       停車
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
L <訊息>\n                                debug 訊息
```

不用 ROS2 也可以直接看序列埠（需要 dialout 權限，或用 Docker）：
```bash
./monitor.sh
```
