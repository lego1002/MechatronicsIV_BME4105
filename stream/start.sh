#!/bin/bash
# 啟動串流，瀏覽器打開 http://<Pi的IP>:8889/cam
#   ./start.sh      前景執行（Ctrl+C 關閉）
#   ./start.sh -d   背景執行（用 ./stop.sh 關閉，log 在 mtx.log）
cd "$(dirname "$0")"
[ -x ./mediamtx ] || { echo "找不到 mediamtx，請先執行 ./install_mediamtx.sh"; exit 1; }
if pgrep -x mediamtx >/dev/null; then
  echo "串流已經在跑了（要重開請先 ./stop.sh）"; exit 1
fi
if [ "$1" = "-d" ]; then
  setsid ./mediamtx mediamtx.yml > mtx.log 2>&1 &
  echo "串流已在背景啟動：http://$(tailscale ip -4 2>/dev/null || hostname -I | cut -d' ' -f1):8889/cam"
else
  exec ./mediamtx mediamtx.yml
fi
