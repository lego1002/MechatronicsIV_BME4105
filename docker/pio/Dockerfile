# PlatformIO 編譯環境（不用在 Pi 上裝任何東西）
FROM python:3.11-slim
RUN pip install --no-cache-dir platformio
ENV PLATFORMIO_CORE_DIR=/pio
WORKDIR /fw
