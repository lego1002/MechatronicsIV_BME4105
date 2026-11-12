# ros2_ws：ROS2 工作空間

ROS2 Humble 跑在 Docker 裡（`docker/ros2/Dockerfile`），Pi 上不用裝 ROS2，只要在 `docker` 群組。

```bash
./run.sh          # 編譯 + 啟動 bridge 和網頁遙控
./run.sh -d       # 同上但在背景執行（log：docker logs -f vacuum-ros2）；../menu.sh 用的就是這個
./run.sh shell    # 進容器下 ros2 指令，例如 ros2 topic echo /imu/yaw
docker stop vacuum-ros2   # 關閉
```

搭配影像串流（`../stream/start.sh -d`），瀏覽器打開 **`http://<Pi的IP>:8080`**：
背景是攝影機畫面，上方是航向刻度帶和角度，左下是手機用的搖桿，右下按鈕或 F 鍵開關吸塵器。

鍵盤配置同 `teleop_twist_keyboard`（但要按住才會動，放開就停）：

```
移動    U I O        加／減速（每次 5%）
        J K L        Q / Z  線速度和角速度一起
        M , .        W / X  只調線速度
                     E / C  只調角速度
```

方向鍵也可以移動，K 或空白鍵停車，0 航向歸零，F 吸塵器。線速度、角速度分開調（預設 40%／25%），手機用面板上的 −／+ 按鈕。「加速」是起步從 0 到全速的時間（預設 0.8s，每次 ±0.1s，存在 Pi 上，Nano 重開會自動補設定）。
STOP 只停馬達，不會關吸塵器；所有網頁都斷線時才會自動關閉。

網頁有密碼（HTTP Basic Auth，帳號隨意、只檢查密碼）。密碼存在 `ros2_ws/teleop_password`（不進 git），
第一次 `./run.sh` 會自動產生並印出來；要改就編輯這個檔再重開。檔案內容清空 = 不檢查密碼。
WebSocket 另外要帶伺服器每次啟動產生的 token（登入後塞在網頁裡），所以不知道密碼也不能直接連 `/ws` 控制馬達。
影像（`:8889`）沒有密碼，只能看不能控制。

## 套件

| 套件 | 功能 | 狀態 |
|---|---|---|
| `vacuum_bridge` | 序列埠和 STM32 溝通，協定見 [firmware/README.md](../firmware/README.md) | ✅ |
| `vacuum_teleop` | 網頁遙控（同一個 port 提供網頁和 WebSocket） | ✅ |
| `vacuum_bringup` | launch 檔（`teleop.launch.py`） | ✅ |
| `vacuum_localization` | 用 yaw＋超音波估計位置 | 🚧 |
| `vacuum_mission` | 任務狀態機 | 🚧 |
| `vacuum_vision` | 紅色圓形偵測 | 🚧 |

## Topic

| Topic | 型別 | 說明 |
|---|---|---|
| `/cmd_vel` | `geometry_msgs/Twist` | `linear.x`、`angular.z`：-1~1（滿速的比例，angular 正 = 左轉）。超過 0.3 秒沒更新就停車 |
| `/imu/yaw` | `std_msgs/Float32` | 航向角（度，-180~180，逆時針為正） |
| `/imu/yaw_rate` | `std_msgs/Float32` | 角速度（度/秒） |
| `/mcu/status` | `std_msgs/UInt8` | 0 正常 / 1 IMU 校正中 / 2 IMU 錯誤 / 255 序列埠斷線 |
| `/mcu/log` | `std_msgs/String` | 韌體的除錯訊息 |
| `/mcu/rtt_ms` | `std_msgs/Float32` | Pi ↔ STM32 來回延遲 |
| `/vacuum/cmd` | `std_msgs/Bool` | 吸塵器開關 |
| `/vacuum/state` | `std_msgs/Bool` | 吸塵器實際狀態（韌體回報，latched） |
| `/motor/accel_ms` | `std_msgs/UInt16` | 設定加速時間（PWM 0→255 的 ms，0 = 不限制），bridge 轉成 `A` 指令 |
| `/motor/accel_state` | `std_msgs/UInt16` | 韌體回報的實際加速時間（latched） |
| `/imu/zero`（服務） | `std_srvs/Trigger` | 航向角歸零 |

## 延遲

控制和遙測走 WebSocket，不經過影像，所以不會被影像拖慢：

| 路徑 | 實測／估計 |
|---|---|
| Pi ↔ STM32（序列埠＋ROS） | 約 2 ms（序列埠本身）、約 10 ms（含 ROS 排程） |
| 網頁 ↔ Pi（WebSocket） | 本機約 1.5 ms；Wi-Fi 通常 5–30 ms（網頁左上角會即時顯示） |
| 影像（WebRTC） | 通常 150–300 ms，是整體延遲的瓶頸 |
