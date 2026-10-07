# 遙控／自走吸塵機器人

台大生機系「機電整合（四）」期末專題。
這台吸塵機器人用 **Raspberry Pi 4（ROS2 Humble）** 搭配 **Arduino Uno**，要完成兩個任務：

- **期中（11/13）**：透過影像串流遠端遙控，在 3m × 3m 的場地內吸紙片後離場
- **期末（12/11）**：自走模式，並重考一次串流遙控。**不使用輪子編碼器**，只靠 IMU（MPU6050）、兩顆超音波（HC-SR04）和攝影機完成定位、吸塵和影像辨識

## 系統概觀

```
筆電／手機 ──WebRTC (Wi-Fi)──▶ Raspberry Pi 4 ◀──USB/UVC── Logitech C310
                               │  ROS2 Humble
                               │  串流（MediaMTX + 硬體 H.264）
                          UART 115200
                               │
                          Arduino Uno ──PWM──▶ L298N ──▶ 直流馬達 ×2
                               ├──I²C──── MPU6050（陀螺儀 → 航向角）
                               └──GPIO─── HC-SR04 ×2（前方、右側測距）
```

詳細設計請看 [docs/architecture.md](docs/architecture.md)。

## 目錄結構

| 路徑 | 內容 | 狀態 |
|---|---|---|
| [`stream/`](stream/) | 低延遲影像串流（WebRTC） | ✅ 可用 |
| [`ros2_ws/`](ros2_ws/) | ROS2 工作空間（bridge、定位、任務狀態機、影像辨識、遙控） | 🚧 規劃中 |
| [`firmware/`](firmware/) | Arduino Uno 韌體（馬達、IMU、超音波） | 🚧 規劃中 |
| [`docs/`](docs/) | 架構設計、Git 教學 | 📝 |
| [`prototypes/`](prototypes/) | 早期測試程式（不維護） | — |

## 快速開始

```bash
git clone <repo網址> vacuum && cd vacuum

# 影像串流（第一次要先下載 MediaMTX）
./stream/install_mediamtx.sh
./stream/start.sh -d      # 瀏覽器打開 http://<Pi的IP>:8889/cam
./stream/stop.sh          # 關閉串流
```

## 硬體

| 元件 | 型號 | 用途 |
|---|---|---|
| 主控電腦 | Raspberry Pi 4 Model B（Ubuntu 22.04） | ROS2、串流、影像辨識 |
| 微控制器 | Arduino Uno | 馬達 PWM、讀取感測器 |
| 攝影機 | Logitech C310（USB） | 串流、辨識圓形標的 |
| IMU | MPU6050 | 陀螺儀 z 軸 → 航向角 |
| 測距 | HC-SR04 × 2 | 量測前方、右側到牆的距離 |
| 馬達驅動 | L298N | 雙馬達差速驅動 |

## 時程

| 日期 | 項目 |
|---|---|
| 10/16 | 第一次報告：設計、時程、分工 |
| 10/30 | 第二次報告：模擬、方塊圖 |
| 11/06 | 期中預演 |
| 11/13 | **期中考**：串流遙控 |
| 11/27、12/04 | 期末預演 |
| 12/11 | **期末考**：自走＋影像辨識 |
| 12/18 | 期末口頭報告（書面報告 12/28 截止） |

## 協作方式

- `main` 分支永遠保持「可以執行」的狀態，**不要直接 push 到 main**
- 每個功能各開一個分支，例如 `feature/arduino-motor`，完成後在 GitHub 發 Pull Request
- 不熟 Git 的話，請看 [docs/github_tutorial/](docs/github_tutorial/)，裡面有一鍵操作的小工具 `git_helper.sh`
