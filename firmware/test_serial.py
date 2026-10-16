"""序列埠協定自動測試（板子不用接任何東西也能跑）：./test.sh"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
s = serial.Serial(port, 115200, timeout=0.1)
fails = 0


def lines(sec):
    out, end, buf = [], time.time() + sec, b''
    while time.time() < end:
        buf += s.read(256)
        *done, buf = buf.split(b'\n')
        out += [l.decode(errors='replace').strip() for l in done]
    return out


def check(name, ok, info=''):
    global fails
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name}  {info}")


def send(cmd):
    s.write((cmd + '\n').encode())


boot = lines(2.5)  # 打開序列埠會讓 Nano 重開
check('boot message', 'L boot vacuum-fw' in boot)

d = [l for l in lines(1.0) if l.startswith('D ')]
check('D rate ~50Hz', 40 <= len(d) <= 55, f'{len(d)} lines/s')
check('D format', all(len(l.split()) == 6 for l in d), d[0] if d else '')

rtts = []
for i in range(10):
    t = time.time()
    send(f'P {i}')
    if f'P {i}' in [l for l in lines(0.1)]:
        rtts.append((time.time() - t) * 1000)
check('ping echo', len(rtts) == 10, f'{len(rtts)}/10')

send('I')
scan = [l for l in lines(0.5) if l.startswith('L i2c scan')]
check('i2c scan', bool(scan), scan[0] if scan else '')

send('V 100 0')
out = lines(0.6)
check('V timeout after 300ms', 'L cmd timeout, stop' in out)

send('X')
check('unknown cmd', 'L unknown cmd: X' in lines(0.3))

send('R')
check('R reboot', 'L boot vacuum-fw' in lines(2.5))

print('ALL PASS' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
