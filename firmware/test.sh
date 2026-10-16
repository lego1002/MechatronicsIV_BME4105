#!/bin/bash
# 序列埠協定自動測試（ping、I2C 掃描、指令逾時停車、重開機）
set -e
cd "$(dirname "$0")"
. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到 Nano 的序列埠，有插上 USB 嗎？"; exit 1; }
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --device="$PORT" \
  --group-add "$(getent group dialout | cut -d: -f3)" -v "$PWD":/fw vacuum-pio python3 test_serial.py "$PORT"
