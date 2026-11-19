#!/usr/bin/env python3
# 馬達方向測試（輪子要架空！）：左輪單獨前進 → 右輪單獨前進 → 兩輪前進 → 原地左轉
# 用法：./motor_dir_test.sh [速度 0~255，預設 120]
import sys
import time

import serial

port = sys.argv[1]
speed = int(sys.argv[2]) if len(sys.argv) > 2 else 120
h = speed // 2

s = serial.Serial(port, 115200, timeout=0.05)
s.dtr = False
time.sleep(0.1)
s.dtr = True  # 重開 MCU，等開機訊息
t0 = time.time()
while time.time() - t0 < 5:
    line = s.readline().decode(errors='ignore').strip()
    if 'boot' in line:
        break
time.sleep(0.5)

# 混合：左 = lin − ang，右 = lin + ang
steps = [
    ('左輪單獨「前進」', h, -h, 3),
    ('停', 0, 0, 1.5),
    ('右輪單獨「前進」', h, h, 3),
    ('停', 0, 0, 1.5),
    ('兩輪一起「前進」', speed, 0, 3),
    ('停', 0, 0, 1.5),
    ('原地左轉（左輪後退、右輪前進）', 0, speed, 3),
    ('停', 0, 0, 1),
]
start = time.time()
for name, lin, ang, dur in steps:
    print(f'{time.time() - start:5.1f}s  {name}  (V {lin} {ang})', flush=True)
    end = time.time() + dur
    while time.time() < end:
        s.write(f'V {lin} {ang}\n'.encode())
        t = time.time() + 0.05
        while time.time() < t:
            line = s.readline().decode(errors='ignore').strip()
            if line.startswith('L ') and 'imu' not in line:
                print('   ', line, flush=True)
s.write(b'S\n')
print('完成')
