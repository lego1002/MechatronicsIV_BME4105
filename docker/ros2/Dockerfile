# ROS2 Humble 執行環境（Pi 上沒有裝 ROS2，全部跑在容器裡）
FROM ros:humble-ros-base
RUN apt-get update && apt-get install -y --no-install-recommends \
      python3-serial python3-pip \
    && rm -rf /var/lib/apt/lists/*
# apt 版的 websockets 9.1 在 Python 3.10 上不能用
RUN pip3 install --no-cache-dir "websockets==12.0"
RUN echo "source /opt/ros/humble/setup.bash" >> /etc/bash.bashrc
WORKDIR /ws
