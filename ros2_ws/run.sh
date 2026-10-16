#!/bin/bash
# 在 Docker 裡編譯並執行 ROS2（Pi 上不用裝 ROS2）
#   ./run.sh          編譯 + 啟動 bridge 和網頁遙控（http://<Pi的IP>:8080）
#   ./run.sh shell    進入容器的 bash（可以用 ros2 topic echo /imu/yaw 等指令）
set -e
cd "$(dirname "$0")"
IMG=vacuum-ros2
docker image inspect $IMG >/dev/null 2>&1 || docker build -t $IMG ../docker/ros2

# 微控制器：Arduino Nano（FT232R / CH340）或 Nucleo（ST-LINK），容器裡一律對應到 /dev/ttyACM0
DEV=$(ls /dev/serial/by-id/usb-FTDI_* /dev/serial/by-id/usb-1a86_* /dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_* 2>/dev/null | head -1)
DEV_ARGS=()
if [ -n "$DEV" ]; then
  DEV_ARGS=(--device="$(readlink -f "$DEV")":/dev/ttyACM0 --group-add "$(getent group dialout | cut -d: -f3)")
else
  echo "⚠ 找不到 Arduino Nano / Nucleo，bridge 會一直重試連線"
fi

TTY=(-i); [ -t 0 ] && TTY=(-it)
RUN=(docker run --rm "${TTY[@]}" --net=host --ipc=host --name vacuum-ros2
     --user "$(id -u):$(id -g)" -e HOME=/tmp -e ROS_LOG_DIR=/ws/log
     -v "$PWD":/ws "${DEV_ARGS[@]}" $IMG)
BUILD='source /opt/ros/humble/setup.bash && colcon build --symlink-install && source install/setup.bash'

if [ "$1" = "shell" ]; then
  if docker ps -q -f name=^vacuum-ros2$ | grep -q .; then
    exec docker exec "${TTY[@]}" --user "$(id -u):$(id -g)" -e HOME=/tmp vacuum-ros2 bash -c "source /opt/ros/humble/setup.bash && source install/setup.bash && exec bash"
  fi
  exec "${RUN[@]}" bash -c "$BUILD && exec bash"
fi
echo "網頁遙控：http://$(tailscale ip -4 2>/dev/null || hostname -I | cut -d' ' -f1):8080"
exec "${RUN[@]}" bash -c "$BUILD && ros2 launch vacuum_bringup teleop.launch.py"
