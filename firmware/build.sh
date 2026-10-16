#!/bin/bash
# 用 Docker 裡的 PlatformIO 編譯韌體（第一次會下載工具鏈，約幾分鐘）
#   ./build.sh                    編譯 Nano（預設）
#   ./build.sh -e nucleo_l432kc   編譯 Nucleo
set -e
cd "$(dirname "$0")"
docker image inspect vacuum-pio >/dev/null 2>&1 || docker build -t vacuum-pio ../docker/pio
mkdir -p ~/.platformio
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v ~/.platformio:/pio -v "$PWD":/fw vacuum-pio pio run "$@"
