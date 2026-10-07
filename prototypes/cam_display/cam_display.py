import cv2
import sys

# 0 代表第一個視訊設備 /dev/video0
cap = cv2.VideoCapture(0)

# 設定解析度（可根據鏡頭規格調整）
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

if not cap.isOpened():
    print("錯誤：無法開啟 USB 攝影機 (/dev/video0)")
    sys.exit(1)

print("攝影機已啟動，按 'q' 鍵退出視窗...")

while True:
    ret, frame = cap.read()
    if not ret:
        print("警告：無法讀取影像影格")
        break

    # 在本機桌面顯示視窗
    cv2.imshow("Raspberry Pi 4 - USB Camera", frame)

    # 偵測按鍵 'q' 退出
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()