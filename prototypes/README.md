# prototypes：早期測試程式

這裡放早期的嘗試，**不維護，也不要在正式系統中使用**。保留下來是為了記錄設計過程。

## cam_display/

用 OpenCV 讀 USB 攝影機，並用 `cv2.imshow` 顯示畫面。

這個方法沒有採用，改用 [`stream/`](../stream/)，原因如下：
- `cv2.imshow` 只會在 Pi 自己的螢幕上顯示，遠端看不到；改用 SSH X11 轉發則要傳沒壓縮的原始畫面（640×480 約 70 Mbps），網路差時傳不動
- Dockerfile 沒有安裝 OpenCV，所以 `import cv2` 會失敗；也沒有設定攝影機裝置和顯示權限
- 沒有壓縮、碼率控制，也沒有斷線恢復機制
