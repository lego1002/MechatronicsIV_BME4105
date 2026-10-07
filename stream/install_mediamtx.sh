#!/bin/bash
# 下載 MediaMTX 執行檔（檔案太大不放進 git，每台 Pi 第一次使用時執行一次）
set -e
VERSION=${VERSION:-v1.21.1}
cd "$(dirname "$0")"
case "$(uname -m)" in
  aarch64) ARCH=arm64 ;;
  armv7l)  ARCH=armv7 ;;
  x86_64)  ARCH=amd64 ;;
  *) echo "不支援的架構：$(uname -m)"; exit 1 ;;
esac
URL="https://github.com/bluenviron/mediamtx/releases/download/${VERSION}/mediamtx_${VERSION}_linux_${ARCH}.tar.gz"
echo "下載 $URL"
curl -fL "$URL" | tar xz mediamtx
echo "完成：$(./mediamtx --version)"
