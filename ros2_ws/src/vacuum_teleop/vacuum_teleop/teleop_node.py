"""網頁遙控：http://<Pi的IP>:8080
同一個 port 提供網頁（index.html）和 WebSocket（/ws）。
影像由 MediaMTX（:8889/cam）提供，網頁用 iframe 嵌進來；這裡只處理遙測和控制，
所以控制延遲不會被影像拖慢。

密碼：參數 password（預設讀環境變數 TELEOP_PASSWORD，run.sh 從 ros2_ws/teleop_password 帶進來）。
網頁用 HTTP Basic Auth（帳號隨意，只檢查密碼）；通過後伺服器把這次啟動產生的 token 塞進網頁，
WebSocket 要帶 ?k=<token> 才接受，所以沒密碼的人不能直接連 /ws 控制馬達。密碼留空 = 不檢查。

瀏覽器 -> 伺服器  {"t":"cmd","v":-1~1,"w":-1~1}   {"t":"zero"}   {"t":"ping","id":n}
伺服器 -> 瀏覽器  {"t":"tel",...} 25Hz           {"t":"pong","id":n}   {"t":"log","msg":...}
"""
import asyncio
import base64
import binascii
import hmac
import json
import os
import secrets
import threading
import time
from http import HTTPStatus

import rclpy
import websockets
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import Twist
from rclpy.node import Node
from std_msgs.msg import Float32, String, UInt8
from std_srvs.srv import Trigger


class Teleop(Node):
    def __init__(self):
        super().__init__('teleop')
        self.web_port = self.declare_parameter('web_port', 8080).value
        self.cmd_timeout = self.declare_parameter('cmd_timeout', 0.3).value
        self.password = self.declare_parameter('password', os.environ.get('TELEOP_PASSWORD', '')).value

        self.state = {'yaw': 0.0, 'rate': 0.0, 'st': 255, 'rtt': None}
        self.logs = []
        self.cmd = (0.0, 0.0)
        self.cmd_time = 0.0

        self.create_subscription(Float32, 'imu/yaw', lambda m: self.state.update(yaw=m.data), 10)
        self.create_subscription(Float32, 'imu/yaw_rate', lambda m: self.state.update(rate=m.data), 10)
        self.create_subscription(UInt8, 'mcu/status', lambda m: self.state.update(st=m.data), 10)
        self.create_subscription(Float32, 'mcu/rtt_ms', lambda m: self.state.update(rtt=m.data), 10)
        self.create_subscription(String, 'mcu/log', lambda m: self.logs.append(m.data), 10)
        self.pub_cmd = self.create_publisher(Twist, 'cmd_vel', 10)
        self.zero_client = self.create_client(Trigger, 'imu/zero')
        self.create_timer(0.05, self.publish_cmd)

    def set_cmd(self, v, w):
        self.cmd = (max(-1.0, min(1.0, float(v))), max(-1.0, min(1.0, float(w))))
        self.cmd_time = time.monotonic()

    def publish_cmd(self):
        # 瀏覽器斷線或分頁切走時，指令會過期 -> 送 0
        v, w = self.cmd if time.monotonic() - self.cmd_time < self.cmd_timeout else (0.0, 0.0)
        msg = Twist()
        msg.linear.x = v
        msg.angular.z = w
        self.pub_cmd.publish(msg)

    def zero(self):
        if self.zero_client.service_is_ready():
            self.zero_client.call_async(Trigger.Request())


def serve(node):
    web_dir = os.path.join(get_package_share_directory('vacuum_teleop'), 'web')
    clients = set()
    token = secrets.token_urlsafe(16)  # 每次啟動都換，重開後網頁會要求重新登入
    if not node.password:
        node.get_logger().warn('沒有設定密碼（TELEOP_PASSWORD），任何人都能控制馬達')

    def password_ok(headers):
        try:
            scheme, cred = headers.get('Authorization', '').split(' ', 1)
            pw = base64.b64decode(cred).decode().split(':', 1)[1]
        except (ValueError, IndexError, binascii.Error, UnicodeDecodeError):
            return False
        return scheme.lower() == 'basic' and hmac.compare_digest(pw.encode(), node.password.encode())

    async def process_request(path, headers):
        if path.startswith('/ws'):
            if node.password and not hmac.compare_digest(path, f'/ws?k={token}'):
                return HTTPStatus.FORBIDDEN, [], b'bad token\n'
            return None  # 交給 WebSocket
        if node.password and not password_ok(headers):
            return HTTPStatus.UNAUTHORIZED, [('WWW-Authenticate', 'Basic realm="vacuum", charset="UTF-8"')], \
                '需要密碼\n'.encode()
        with open(os.path.join(web_dir, 'index.html'), 'rb') as f:
            body = f.read().replace(b'__WS_TOKEN__', token.encode())
        return HTTPStatus.OK, [('Content-Type', 'text/html; charset=utf-8'),
                               ('Cache-Control', 'no-cache')], body

    async def handler(ws, path):
        clients.add(ws)
        try:
            async for raw in ws:
                try:
                    m = json.loads(raw)
                except ValueError:
                    continue
                t = m.get('t')
                if t == 'cmd':
                    node.set_cmd(m.get('v', 0), m.get('w', 0))
                elif t == 'ping':
                    await ws.send(json.dumps({'t': 'pong', 'id': m.get('id')}))
                elif t == 'zero':
                    node.zero()
        finally:
            clients.discard(ws)
            if not clients:
                node.set_cmd(0, 0)

    async def broadcast():
        while rclpy.ok():
            await asyncio.sleep(0.04)  # 25Hz
            if not clients:
                node.logs.clear()
                continue
            msgs = [json.dumps({'t': 'tel', **node.state})]
            while node.logs:
                msgs.append(json.dumps({'t': 'log', 'msg': node.logs.pop(0)}))
            for ws in list(clients):
                for msg in msgs:
                    try:
                        await ws.send(msg)
                    except websockets.ConnectionClosed:
                        break

    async def main():
        async with websockets.serve(handler, '0.0.0.0', node.web_port,
                                    process_request=process_request, compression=None):
            node.get_logger().info(f'web teleop on http://<Pi IP>:{node.web_port}')
            await broadcast()

    asyncio.run(main())


def main():
    rclpy.init()
    node = Teleop()
    threading.Thread(target=rclpy.spin, args=(node,), daemon=True).start()
    try:
        serve(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
