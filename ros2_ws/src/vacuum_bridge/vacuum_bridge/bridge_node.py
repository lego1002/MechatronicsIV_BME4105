"""Arduino Nano <-> ROS2 序列埠橋接。協定見 firmware/README.md。

訂閱  /cmd_vel        geometry_msgs/Twist   linear.x、angular.z 範圍 -1~1（滿速的比例，angular 正 = 左轉）
發布  /imu/yaw        std_msgs/Float32      航向角（度，-180~180，逆時針為正）
      /imu/yaw_rate   std_msgs/Float32      角速度（度/秒）
      /mcu/status     std_msgs/UInt8        0 正常 / 1 IMU 校正中 / 2 IMU 錯誤 / 255 序列埠斷線
      /mcu/log        std_msgs/String       韌體的 L 訊息
      /mcu/rtt_ms     std_msgs/Float32      Pi <-> Nano 來回延遲
服務  /imu/zero       std_srvs/Trigger      航向角歸零（車子要靜止 2 秒）
"""
import threading
import time

import rclpy
import serial
from geometry_msgs.msg import Twist
from rclpy.node import Node
from std_msgs.msg import Float32, String, UInt8
from std_srvs.srv import Trigger


class Bridge(Node):
    def __init__(self):
        super().__init__('bridge')
        self.port = self.declare_parameter('port', '/dev/ttyACM0').value
        self.baud = self.declare_parameter('baud', 115200).value
        self.cmd_timeout = self.declare_parameter('cmd_timeout', 0.3).value

        self.pub_yaw = self.create_publisher(Float32, 'imu/yaw', 10)
        self.pub_rate = self.create_publisher(Float32, 'imu/yaw_rate', 10)
        self.pub_status = self.create_publisher(UInt8, 'mcu/status', 10)
        self.pub_log = self.create_publisher(String, 'mcu/log', 10)
        self.pub_rtt = self.create_publisher(Float32, 'mcu/rtt_ms', 10)
        self.create_subscription(Twist, 'cmd_vel', self.on_cmd, 10)
        self.create_service(Trigger, 'imu/zero', self.on_zero)

        self.ser = None
        self.write_lock = threading.Lock()
        self.cmd = (0, 0)
        self.cmd_time = 0.0
        self.ping_sent = {}

        self.create_timer(0.05, self.send_cmd)    # 20Hz 送馬達指令（兼心跳）
        self.create_timer(1.0, self.send_ping)
        threading.Thread(target=self.reader, daemon=True).start()

    # ---------- 序列埠 ----------
    def write(self, line):
        with self.write_lock:
            if self.ser is None:
                return
            try:
                self.ser.write((line + '\n').encode())
            except (serial.SerialException, OSError):
                pass  # reader 執行緒會負責重連

    def reader(self):
        while rclpy.ok():
            try:
                with self.write_lock:
                    self.ser = serial.Serial(self.port, self.baud, timeout=0.5)
                self.get_logger().info(f'connected {self.port}')
                self.ser.reset_input_buffer()
                while rclpy.ok():
                    raw = self.ser.readline()
                    if raw:
                        self.on_line(raw.decode(errors='replace').strip())
            except (serial.SerialException, OSError) as e:
                self.get_logger().warn(f'serial: {e}，1 秒後重試', throttle_duration_sec=10)
                with self.write_lock:
                    if self.ser is not None:
                        self.ser.close()
                    self.ser = None
                self.pub_status.publish(UInt8(data=255))
                time.sleep(1.0)

    def on_line(self, line):
        parts = line.split(' ', 1)
        if parts[0] == 'D' and len(parts) == 2:
            f = parts[1].split()
            if len(f) < 5:
                return
            try:
                self.pub_yaw.publish(Float32(data=float(f[0])))
                self.pub_rate.publish(Float32(data=float(f[1])))
                self.pub_status.publish(UInt8(data=int(f[4])))
            except ValueError:
                pass
        elif parts[0] == 'L' and len(parts) == 2:
            self.get_logger().info(f'mcu: {parts[1]}')
            self.pub_log.publish(String(data=parts[1]))
        elif parts[0] == 'P' and len(parts) == 2:
            t0 = self.ping_sent.pop(parts[1], None)
            if t0 is not None:
                self.pub_rtt.publish(Float32(data=(time.monotonic() - t0) * 1000))

    # ---------- ROS ----------
    def on_cmd(self, msg):
        def scale(x):
            return int(round(max(-1.0, min(1.0, x)) * 255))
        self.cmd = (scale(msg.linear.x), scale(msg.angular.z))
        self.cmd_time = time.monotonic()

    def send_cmd(self):
        v, w = self.cmd if time.monotonic() - self.cmd_time < self.cmd_timeout else (0, 0)
        self.write(f'V {v} {w}')

    def send_ping(self):
        n = str(int(time.monotonic() * 1000) % 1000000)
        self.ping_sent = {n: time.monotonic()}
        self.write(f'P {n}')

    def on_zero(self, req, res):
        self.write('Z')
        res.success = self.ser is not None
        res.message = 'calibrating, keep still 2s' if res.success else 'serial not connected'
        return res


def main():
    rclpy.init()
    node = Bridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.write('S')
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
