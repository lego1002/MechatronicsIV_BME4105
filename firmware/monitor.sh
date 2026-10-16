#!/bin/bash
# 看 Nano 的序列埠輸出（Ctrl+] 離開）
#   ./monitor.sh            互動模式（可以打指令，例如 I、V 100 0）
#   ./monitor.sh 5          只讀 5 秒後結束（打開序列埠會讓 Nano 重開）
set -e
cd "$(dirname "$0")"
. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到 Nano 的序列埠，有插上 USB 嗎？"; exit 1; }
ARGS=(--rm --user "$(id -u):$(id -g)" -e HOME=/tmp --device="$PORT" --group-add "$(getent group dialout | cut -d: -f3)")
if [ -n "$1" ]; then
  exec docker run "${ARGS[@]}" vacuum-pio python3 -c "
import serial, sys, time
s = serial.Serial('$PORT', 115200, timeout=0.2)
end = time.time() + float(sys.argv[1])
while time.time() < end:
    sys.stdout.write(s.read(256).decode(errors='replace')); sys.stdout.flush()
" "$1"
fi
exec docker run -it "${ARGS[@]}" vacuum-pio python3 -m serial.tools.miniterm --eol LF "$PORT" 115200
