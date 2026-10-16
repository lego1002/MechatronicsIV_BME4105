#!/bin/bash
# 燒錄
#   ./flash.sh          Nano：編譯後用 avrdude 經 USB 序列埠燒錄（會自動重開）
#   ./flash.sh nucleo   Nucleo：把 firmware.bin 複製到隨身碟 NODE_L432KC，ST-LINK 自動寫入並重開
set -e
cd "$(dirname "$0")"
if [ "$1" = "nucleo" ]; then
  ./build.sh -e nucleo_l432kc
  BIN=.pio/build/nucleo_l432kc/firmware.bin
  MNT=$(findmnt -rn -o TARGET -S LABEL=NODE_L432KC 2>/dev/null || ls -d /media/"$USER"/NODE_L432KC 2>/dev/null)
  [ -n "$MNT" ] || { echo "找不到 NODE_L432KC，Nucleo 有插上 USB 嗎？"; exit 1; }
  cp "$BIN" "$MNT"/ && sync
  echo "已燒錄到 $MNT，Nucleo 會自動重開"
  exit 0
fi

. ./port.sh
PORT=$(find_port)
[ -n "$PORT" ] || { echo "找不到 Nano 的序列埠，有插上 USB 嗎？"; exit 1; }
if docker ps -q -f name=^vacuum-ros2$ | grep -q .; then
  echo "⚠ vacuum-ros2 正在使用序列埠，先 docker stop vacuum-ros2"; exit 1
fi
docker image inspect vacuum-pio >/dev/null 2>&1 || docker build -t vacuum-pio ../docker/pio
mkdir -p ~/.platformio
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  --device="$PORT" --group-add "$(getent group dialout | cut -d: -f3)" \
  -v ~/.platformio:/pio -v "$PWD":/fw vacuum-pio pio run -e nano -t upload --upload-port "$PORT"
echo "已燒錄到 $PORT"
