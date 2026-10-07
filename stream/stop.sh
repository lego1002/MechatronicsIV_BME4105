#!/bin/bash
# 關閉串流（MediaMTX 會順便關掉它啟動的攝影機程式）
if ! pgrep -x mediamtx >/dev/null; then
  echo "串流本來就沒有在跑"
  exit 0
fi
pkill -x mediamtx
for _ in $(seq 10); do
  pgrep -x mediamtx >/dev/null || break
  sleep 0.5
done
pkill -9 -x mediamtx 2>/dev/null        # 5 秒還沒結束就強制關閉
pkill -9 -x gst-launch-1.0 2>/dev/null  # 保險：殘留的攝影機程式
echo "串流已關閉"
