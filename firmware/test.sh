#!/bin/bash
# 序列埠協定自動測試（ping、I2C 掃描、指令逾時停車、重開機、看門狗）
#   ./test.sh              馬達會短暫轉動，輪子要架空
#   ./test.sh --no-motor   跳過會讓馬達轉動的測試
set -e
cd "$(dirname "$0")"
. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到 Nano 的序列埠，有插上 USB 嗎？"; exit 1; }
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --device="$PORT" \
  --group-add "$(getent group dialout | cut -d: -f3)" -v "$PWD":/fw vacuum-pio python3 test_serial.py "$PORT" "$@"
