# stream：低延遲影像串流

把 USB 攝影機的畫面，用 **Pi 4 的硬體 H.264 編碼器**壓縮後，透過 **MediaMTX** 以 **WebRTC** 送到瀏覽器。
這套串流是針對比賽現場「網路可能很差」的情況設計的。

```
C310 (YUYV 640×480) → v4l2h264enc (~1 Mbps) → RTP/UDP :5004 → MediaMTX → WebRTC :8889 / RTSP :8554
```

## 使用方式

```bash
./install_mediamtx.sh   # 第一次使用：下載 MediaMTX 執行檔
./start.sh -d           # 背景啟動（不加 -d 就是前景執行，按 Ctrl+C 關閉）
./stop.sh               # 關閉
```

| 觀看方式 | 網址 |
|---|---|
| 瀏覽器（主要） | `http://<Pi的IP>:8889/cam` |
| VLC（備援） | `rtsp://<Pi的IP>:8554/cam` |

`<Pi的IP>` 可以填 Tailscale IP（`tailscale ip -4`）或區網 IP（`hostname -I`）。

## 調整參數

修改 `mediamtx.yml` 的 `runOnInit`，用 `env` 帶入環境變數（MediaMTX 不經過 shell 執行指令），例如：

```yaml
runOnInit: env BITRATE=500000 W=320 H=240 ./cam.sh
```

| 變數 | 預設值 | 說明 |
|---|---|---|
| `W` / `H` | 640 / 480 | 解析度 |
| `FPS` | 30 | 幀率（C310 在暗處會自動降到約 13） |
| `BITRATE` | 1000000 | 位元率（bps）。網路差就調低 |
| `GOP` | 15 | 每幾幀一個關鍵幀。越小，掉封包後越快恢復 |
| `DEV` | /dev/video0 | 攝影機裝置 |

## 設計重點

| 設計 | 原因 |
|---|---|
| 硬體 H.264 | 頻寬約為 MJPEG 的 1/5～1/10，CPU 用量約 15–20% |
| WebRTC（UDP） | 掉封包時直接跳過，延遲不會累積；UDP 被擋時自動改走 TCP |
| 關鍵幀間隔短 | 掉封包造成花屏後，約 0.5 秒就會恢復 |
| 不依賴 STUN 或網際網路 | 現場沒有外網也能用 |
| 看門狗（`cam.sh`） | 畫面 3 秒沒更新就重啟攝影機，由 MediaMTX 自動重新啟動 |

## 比賽現場建議

1. 不要依賴場地網路：自己帶路由器或用手機熱點，筆電和 Pi 連同一個 Wi-Fi，直接用區網 IP 連線
2. 關閉 Pi 的 Wi-Fi 省電模式：`sudo iw wlan0 set power_save off`
3. 盡量使用 5 GHz Wi-Fi
