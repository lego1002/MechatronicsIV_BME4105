# ros2_ws：ROS2 工作空間

> 🚧 尚未開始。環境：Ubuntu 22.04 + ROS2 Humble

## 規劃中的套件

| 套件 | 功能 |
|---|---|
| `vacuum_bridge` | 透過序列埠和 Arduino 溝通 |
| `vacuum_localization` | 用 yaw＋超音波估計位置 |
| `vacuum_mission` | 任務狀態機 |
| `vacuum_vision` | 紅色圓形偵測 |
| `vacuum_teleop` | 網頁遙控 |
| `vacuum_bringup` | launch 檔、參數 |

## 編譯

```bash
cd ros2_ws
colcon build --symlink-install
source install/setup.bash
```

設計細節見 [docs/architecture.md](../docs/architecture.md)。
