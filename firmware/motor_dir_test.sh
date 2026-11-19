#!/bin/bash
# 馬達方向測試（輪子要架空！）：左輪前進 → 右輪前進 → 兩輪前進 → 原地左轉，各 3 秒
#   ./motor_dir_test.sh [速度 0~255，預設 120]
set -e
cd "$(dirname "$0")"
. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到序列埠，有插上 USB 嗎？"; exit 1; }
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --device="$PORT" \
  --group-add "$(getent group dialout | cut -d: -f3)" -v "$PWD":/fw -w /fw vacuum-pio python3 motor_dir_test.py "$PORT" "${1:-120}"
