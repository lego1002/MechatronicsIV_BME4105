"""馬達對 IMU 的影響測試：./imu_motor_test.sh [速度 0-255，預設 120]

⚠ 車子要架高、輪子懸空！車身不動，所以陀螺儀量到的任何角速度都是馬達造成的干擾
（電源雜訊、震動、I2C 錯誤）。分別在「不限制加減速」和「有限制」下跑同一組動作，比較：
  - rate 的平均／標準差／最大值（°/s，理想是 0）
  - yaw 漂移（°，理想是 0）
  - I2C 讀取錯誤次數、IMU 斷線次數
"""
import statistics
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
SPEED = max(0, min(255, int(sys.argv[2]))) if len(sys.argv) > 2 else 120  # 先低速，確認電源穩了再加
s = serial.Serial(port, 115200, timeout=0.05)
buf = b''


def read_lines():
    global buf
    buf += s.read(4096)
    *done, buf = buf.split(b'\n')
    return [l.decode(errors='replace').strip() for l in done]


def send(cmd):
    s.write((cmd + '\n').encode())


def run(lin, ang, sec):
    """持續送 V 指令 sec 秒，回傳期間收到的 D／L 行"""
    d, logs, end = [], [], time.time() + sec
    while time.time() < end:
        send(f'V {lin} {ang}')
        time.sleep(0.05)
        for l in read_lines():
            (d if l.startswith('D ') else logs).append(l)
    return d, logs


def wait_ready():
    end = time.time() + 6
    while time.time() < end:
        for l in read_lines():
            if l.startswith('D ') and l.split()[-1] == '0':
                return True
    return False


# 自己拉 DTR 讓 Nano 重開並校正（avrdude 會清掉 hupcl，開序列埠不一定會重開）
s.dtr = False
time.sleep(0.1)
s.reset_input_buffer()
s.dtr = True
if not wait_ready():
    sys.exit('IMU 沒有就緒（status 不是 0），先用 ./monitor.sh 5 檢查')

# (名稱, lin, ang, 秒)：直行、反轉、原地旋轉，最後停下來看有沒有殘留
SEQ = [('靜止基準', 0, 0, 2), ('前進', SPEED, 0, 2), ('瞬間反轉', -SPEED, 0, 2),
       ('原地左轉', 0, SPEED, 2), ('原地右轉', 0, -SPEED, 2), ('停車', 0, 0, 1.5)]
print(f'測試速度 {SPEED}/255')

results = {}
for ramp in (0, 300):
    send(f'A {ramp}')
    send('Z')  # 每輪重新歸零校正，兩輪起點一樣
    time.sleep(0.3)
    s.reset_input_buffer()  # 丟掉 Z 之前的 D（status 0），才不會誤判校正完成
    buf = b''
    wait_ready()
    print(f'\n===== 加減速限制 A {ramp}（{"不限制" if ramp == 0 else f"0→255 花 {ramp}ms"}）=====')
    print(f'{"動作":<8}{"rate平均":>9}{"標準差":>8}{"最大":>8}{"yaw變化":>9}  其他')
    total = {'errs': 0, 'lost': 0, 'maxabs': 0.0}
    yaw0 = None
    for name, lin, ang, sec in SEQ:
        d, logs = run(lin, ang, sec)
        if lin == 0 and ang == 0:
            send('S')
        ok = [l.split() for l in d if len(l.split()) == 6]
        rates = [float(x[2]) for x in ok]
        yaws = [float(x[1]) for x in ok]
        status = {x[5] for x in ok}
        errs = sum(int(l.split()[-1]) for l in logs if l.startswith('L imu read errors'))
        lost = sum('imu lost' in l for l in logs)
        total['errs'] += errs
        total['lost'] += lost
        if not rates:
            print(f'{name:<8}  沒有資料 {logs}')
            continue
        if yaw0 is None:
            yaw0 = yaws[0]
        mx = max(abs(r) for r in rates)
        total['maxabs'] = max(total['maxabs'], mx)
        extra = []
        if errs: extra.append(f'I2C錯誤 {errs}')
        if lost: extra.append(f'IMU斷線 {lost}')
        if status - {'0'}: extra.append(f'status {sorted(status)}')
        print(f'{name:<8}{statistics.mean(rates):9.2f}{statistics.pstdev(rates):8.2f}{mx:8.2f}'
              f'{yaws[-1] - yaws[0]:9.2f}  {" ".join(extra)}')
    send('S')
    drift = yaws[-1] - yaw0 if rates else float('nan')
    total['drift'] = drift
    results[ramp] = total
    print(f'總 yaw 漂移 {drift:.2f}°，最大 |rate| {total["maxabs"]:.2f}°/s，'
          f'I2C 錯誤 {total["errs"]}，IMU 斷線 {total["lost"]}')
    time.sleep(1)

send('A 300')
print('\n===== 結論 =====')
for ramp, t in results.items():
    print(f'A {ramp:<4} 漂移 {t["drift"]:7.2f}°  最大|rate| {t["maxabs"]:6.2f}°/s  I2C錯誤 {t["errs"]}  斷線 {t["lost"]}')
print('判讀：輪子懸空時 |rate| 應該 < 1°/s、漂移 < 1°。'
      '遠大於這個 = IMU 真的受馬達影響；有限制明顯比較好 = 主要是電流突波造成的。')
