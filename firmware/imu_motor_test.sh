#!/bin/bash
# 馬達對 IMU 的影響測試（車子要架高、輪子懸空！）
# 用法：./imu_motor_test.sh [速度 0-255，預設 120]
set -e
cd "$(dirname "$0")"
. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到 Nano 的序列埠，有插上 USB 嗎？"; exit 1; }
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --device="$PORT" \
  --group-add "$(getent group dialout | cut -d: -f3)" -v "$PWD":/fw vacuum-pio python3 imu_motor_test.py "$PORT" "${1:-120}"
