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
- TB6612FNG 雙馬達差速驅動（v2.2 板：PWMA = D9、PWMB = D10，Timer1 硬體 PWM 約 31kHz）
- 安全機制：300ms 沒收到指令就停車
- 防當機（Nano）：看門狗 500ms，主迴圈卡住就自動重開，重開後馬達和吸塵器都是關閉的，開機回報 `L reset cause: watchdog` 或 `other`（`other` = 上電、壓降、reset 鍵或 DTR，bootloader 不提供細分）。bridge 超過 3 秒沒收到資料會再用 DTR 硬體重置；重開後 IMU 要重新校正
- 吸塵器：D5 控制 MOSFET（高電位開）＋ D12 控制繼電器（低電位開），`F` 同時切換兩個，開機預設關閉；2 秒沒收到任何指令會自動關閉（`S` 和馬達逾時不會關吸塵器）
- 梯形加減速（PWM 0→255：加速預設 800ms、減速 300ms，反轉時先減到 0 再加速；`A` 指令可調，網頁也能調加速）：避免突然全速／正反轉抽大電流拉垮電池、干擾 IMU
- IMU 偶發 I²C 錯誤直接跳過那一筆，連續 10 次失敗才當作斷線；重連後沿用原本的偏移和航向，不會在車子移動時重新校正
- I2C 有逾時（線鬆掉不會卡死），IMU 斷線會每秒自動重試

## 接線（v2.2 洞洞板，Arduino UNO／Nano 腳位相同）

2026-11-20 換成 v2.2 新洞洞板＋Arduino UNO，接腳全部改過（舊板的 D6/D7 PWM、D12 繼電器、STBY 接 3.3V 都不再適用）。

| 元件 | 腳位 | 備註 |
|---|---|---|
| MPU6050 SDA / SCL | A4 / A5 | 洞洞板 IMU 端子，VCC 接 5V |
| TB6612 PWMA / PWMB | **D9 / D10** | 右 / 左馬達（`MOTOR_SWAP_LR`）。都是 Timer1 硬體 PWM，開機改成約 31kHz（`MOTOR_PWM_31K`，聽不到嘯叫） |
| TB6612 AIN1 / AIN2 | A3 / A2 | |
| TB6612 BIN1 / BIN2 | A1 / A0 | 新板方向還沒確認，輪子反轉就在 `config.h` 對調 |
| TB6612 STBY | **D8** | 開機拉高 |
| TB6612 VCC / VM | 5V / 動力 + | GND 要和 UNO、電池負極共地 |
| 吸塵風扇 MOSFET | **D5** | IRLZ44N 低側開關（220Ω 到閘極、10kΩ 下拉），**高電位 = 開**。目前 MOSFET 損壞、風扇常轉，換新的之後 `F` 才有作用 |
| 吸塵風扇 繼電器 IN | **D12** | 2026-11-20 新增（v1 車用）。JQC-3FF-S-Z，**低電位吸合**（`VACUUM_RELAY_ON_LEVEL`）；VCC/GND 接 UNO 5V/GND，風扇接 COM/NO。`F` 指令同時切換 MOSFET 和繼電器，哪台車接哪種都能用；開機時先輸出關閉電位，不會吸合一下 |
| 相機搖臂伺服 訊號 | **D11**（2026-11-20 從 D12 移過來） | SG90／MG90S。Timer2 中斷產生 50Hz 脈衝（Servo.h 會佔 Timer1，讓 D9/D10 馬達 PWM 失效）；因此 D3/D11 不能 analogWrite。紅線接 Mini560 5V（洞洞板 S9），棕線接 GND（S12） |
| 板載 LED | D13 | 校正中快閃、正常慢閃、IMU 錯誤恆亮 |
| 前超音波 TRIG / ECHO | D4 / D2 | 已焊端子，韌體還沒實作 |
| 右超音波 TRIG / ECHO | D7 / D3 | 已焊端子，韌體還沒實作 |
| 電池分壓 | A6（僅 Nano） | 10k / 3.3k。**UNO 沒有 A6**，換 UNO 後暫時讀不到電池電壓 |
| 空著 | D6 | |

注意：Pi 每次打開序列埠（例如啟動 bridge）Nano 都會自動重開，約 1.5 秒後才開始送資料。

Nucleo-L432KC 的接線和已知問題（A4/A5 錫橋、A7 是 VCP TX）見 git 歷史或 `src/config.h` 的 Nucleo 段落。

## 序列埠協定（Nano：/dev/ttyUSB0，Nucleo：/dev/ttyACM0；115200 baud）

Pi → MCU：
```
V <線速度 -255~255> <角速度 -255~255>\n   移動（角速度正 = 左轉），要持續送，300ms 沒收到就停
S\n                                       停車（立刻放掉，不經過加減速）
A <加速ms> [減速ms]\n                    梯形加減速：PWM 0→255 至少花幾毫秒，0 = 不限制（預設 800 / 300）
                                          只給一個數 = 加減速都設成它；回 L ramp accel N decel M ms
Z\n                                       航向角歸零並重新估計偏移（車子要靜止 2 秒）
P <任意字串>\n                            原樣回傳，用來量延遲
I\n                                       掃描 I²C（除錯用）；另外回 SDA/SCL 取樣 1000 次的低電位次數（閒置時都該是 0；一直低 = 線被拉住）
R\n                                       重新開機（看開機訊息用）
H <0|1>\n                                 測試用，故意卡死：0 = 等看門狗重開，1 = 關掉看門狗，等 DTR 重置（僅 Nano）
F <0|1>\n                                 吸塵器關／開，回 `L vacuum on|off`；2 秒沒收到任何指令會自動關閉
C <角度 0~180 | -1>\n                      相機搖臂伺服：轉到該角度（限速 150°/s），-1 = 放鬆不送脈衝；回 `L cam N` / `L cam off`
                                          開機時不送脈衝，等 Pi 送 C（bridge 連線和收到 L boot 時會補送）
B <0|1>\n                                 蜂鳴器（還沒接）
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
