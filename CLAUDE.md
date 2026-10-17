# CLAUDE.md — handoff notes for the next agent

Vacuum robot, final project for NTU BIME "Mechatronics IV". Raspberry Pi 4 (Ubuntu 22.04, ROS2 Humble in Docker)
plus a microcontroller that drives the motors and reads sensors. Details are in `README.md` and `docs/architecture.md`.

- **Midterm 11/13**: remote teleop over the video stream: vacuum paper scraps in a 3m×3m arena, then exit.
- **Final 12/11**: autonomous run plus a teleop retake. **No wheel encoders.** Only MPU6050 yaw, 2× HC-SR04 (front and right) and the camera.
  Speed score (65) > suction score (32). Route plan: wall-following clockwise, see `docs/architecture.md` §3.

## Conventions

- All docs, code comments and log text for humans are **Traditional Chinese**. Match that style. Serial protocol tokens stay ASCII.
- Keep READMEs in sync when behavior changes (`firmware/README.md` documents the serial protocol, the single source of truth).
- `main` must stay runnable. Team workflow is feature branches + PRs (see README). Commit only when the user asks.
- Nothing is installed natively on the Pi. **Everything runs in Docker** (user `jamie` is in the `docker` group):
  - `vacuum-pio` (`docker/pio/Dockerfile`): PlatformIO, toolchains cached in `~/.platformio`. Also has python3 + pyserial.
  - `vacuum-ros2` (`docker/ros2/Dockerfile`): ROS2 Humble + pyserial + websockets 12.0.
- The host has no dialout access for scripts by default. Containers get `--device` + `--group-add dialout`.

## Hardware status (as of 2026-10-16)

- **MCU switched from STM32 Nucleo-L432KC to Arduino Nano** (ATmega328P, FT232R USB-serial, Optiboot @115200,
  PlatformIO board `nanoatmega328new`). It shows up as `/dev/ttyUSB0`
  (`/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A5069RR4-if00-port0`). The Nucleo code is kept and still builds.
- Motor driver: TB6612FNG, 2 DC motors (differential drive). The old docs mention L298N/Uno: outdated.
- Camera: Logitech C310 on the Pi.
- **Nano wiring confirmed by the user (2026-10-17, soldered perfboard)**, see `firmware/src/config.h`:
  PWMA = D6 (Timer0 HW PWM, 976Hz), PWMB = D7 (**no HW PWM** → Timer1 ISR soft PWM, 976Hz; D9/D10 can't be PWM now),
  AIN1/AIN2 = A3/A2, BIN1/BIN2 = A1/A0, STBY = 3.3V, MPU6050 SDA/SCL = A4/A5. Free: D2–D5, D8–D12.
- 2026-10-17: IMU still not detected: boot log `L i2c idle SDA=0 SCL=0 ext-pullup=NO` means the GY-521 isn't powered or
  isn't reaching A4/A5. Motors not yet observed spinning (needs the user watching, wheels lifted).
- 2026-10-17 `imu_motor_test.sh` (motors on the bench, not on the chassis): ramp off → gyro spikes up to 14°/s on abrupt
  reversal; ramp 300ms → max 0.85°/s, drift <0.1°. Either way ~6–7% of I²C reads fail while motors run (0 at rest):
  brushed-motor EMI, harmless now (samples skipped), fix in hardware (motor caps, twisted wires, separate MPU power/GND).
  Rerun once mounted on the chassis (vibration + higher load current).
- 2026-10-17 18:28: user soldered caps on the motors, then **the Pi hard-reset** (journal just stops, no shutdown; SSH
  dropped). Power: Pi from a USB-C charger, motors (TB6612 VM) from 18650s, so the supplies are separate. The only
  shared path is Pi USB → Nano → TB6612 GND. No container (= nothing sending `V`) was running after 17:38, so the motors
  probably weren't being driven. Suspects: spike when hot-plugging the battery while the Nano is on USB, a solder
  bridge feeding VM into 5V, soldering with USB connected, a marginal charger (<5.1V 3A). Before the next motor run:
  multimeter-check VM/5V/GND for shorts, plug in the battery before the USB, use tmux, and afterwards check
  `cat /sys/devices/platform/soc/soc:firmware/get_throttled` (0 = no undervoltage; `vcgencmd` isn't installed).
- 2026-10-17 ~18:50: **Pi + everything now on 18650s.** `imu_motor_test.sh` (speed 120, wheels lifted, user present):
  IMU detected, **0 I²C errors** (was 6–7%, motor caps helped), `get_throttled` = 0, Pi didn't reset.
  A 0: max 3.67°/s spike on forward start, drift −0.16°. A 300: max 0.17°/s, drift 0.02° (same as at rest). Keep A 300.
  Still to confirm with the user: did each wheel actually spin, and in the right direction?
- `imu_motor_test.sh [speed]` now defaults to **speed 120/255** (was full 255). Raise it only once the power is stable.
  Expected motion with wheels lifted (the sequence runs twice: `A 0` then `A 300`, ~30s total incl. calibration):
  2s still → 2s both wheels forward → 2s both backward (abrupt with `A 0`, smooth with `A 300`) → 2s left back +
  right forward (spin left) → 2s left forward + right back (spin right) → 1.5s stop. A wheel spinning the wrong
  way = swap that motor's wires (or its xIN1/xIN2 in `config.h`). A wheel not spinning = check VM, STBY, PWM pin.
- Gotcha: avrdude leaves the tty with `-hupcl`, so opening the port may NOT reset the Nano. `test_serial.py` pulses DTR itself.

## Components and status

| Area | Status | Notes |
|---|---|---|
| `stream/` | ✅ works | C310 → Pi HW H.264 (v4l2h264enc) → MediaMTX → WebRTC `:8889/cam`, RTSP `:8554/cam`. `./stream/start.sh -d` / `stop.sh` |
| `firmware/` | 🚧 | IMU yaw ✅ (validated on Nucleo), serial protocol ✅ (tested on Nano), motors untested on Nano, ultrasonic/buzzer/fan not implemented (`D` reports `-1 -1`; `B`/`F` cmds are stubs) |
| `ros2_ws/vacuum_bridge` | ✅ | serial ↔ ROS topics; `/cmd_vel` −1..1 scaled to ±255 `V` cmds at 20Hz; ping every 1s → `/mcu/rtt_ms` |
| `ros2_ws/vacuum_teleop` | ✅ | web page + WebSocket on `:8080` (camera background, yaw tape, joystick/WASD, space = stop, Z = zero) |
| `ros2_ws/vacuum_bringup` | ✅ | `teleop.launch.py` |
| localization / mission / vision | ❌ not started | planned packages `vacuum_localization`, `vacuum_mission`, `vacuum_vision` (red circle, 20cm) |

## Commands

```bash
# Firmware (in firmware/)
./build.sh                     # compile Nano (default env)
./build.sh -e nucleo_l432kc    # compile old Nucleo
./flash.sh                     # build + avrdude upload to Nano (refuses if vacuum-ros2 is running)
./flash.sh nucleo              # copy .bin to NODE_L432KC mass storage
./monitor.sh                   # interactive miniterm (needs a TTY; agents should use `./monitor.sh 5`)
./monitor.sh 5                 # read 5s of output and exit (good for agents)
./imu_motor_test.sh [speed]    # wheels LIFTED: gyro noise/drift/I2C errors with ramp off vs 300ms (speed default 120)
./test.sh                      # automated protocol test (test_serial.py): boot msg, 50Hz D, ping, I2C scan, 300ms timeout, unknown cmd, R reboot

# ROS2 (in ros2_ws/)
./run.sh                       # colcon build + launch bridge + web teleop; maps Nano/Nucleo serial to /dev/ttyACM0 in container
./run.sh shell                 # shell in the (running) container
docker stop vacuum-ros2
```

The agent can build, flash and test the firmware on its own via these scripts. Last run: Nano build has 0 warnings
(~11.8KB flash, ~1KB RAM), both envs build, `./test.sh` passes 8/8.

## Firmware design notes (`firmware/src/main.cpp`, `config.h`)

- One source file for both boards, platform bits under `#if defined(ARDUINO_ARCH_AVR)`:
  - AVR: D6 analogWrite (Timer0); D7 soft PWM via Timer1 OVF/COMPx ISRs (8-bit fast PWM, /64); `Wire.setWireTimeout(3000,true)`
    (otherwise a loose I²C wire hangs the loop); `R` reset via watchdog (`wdt_enable(WDTO_15MS)`; MCUSR/wdt cleared in setup).
  - STM32: solder-bridge short check (SB16/SB18 tie A4/A5 to D4/D5), TIM2 soft-PWM for pins without timers, `NVIC_SystemReset`.
- IMU: MPU6050 gyro z @200Hz, ±500dps, DLPF 42Hz; 2s still calibration at boot / on `Z`; slow bias tracking when motors
  idle >1s; auto re-init every 1s if IMU lost.
- Motor ramp: `V` sets a target, output slews at 255/`MOTOR_RAMP_MS` (300ms, runtime `A <ms>`, 0 = off). `S`/timeout stop instantly (coast).
- IMU errors: single I²C read failures are skipped (`L imu read errors N` per second); lost only after 10 in a row; re-init keeps bias/yaw (calibrates only once, or on `Z`).
- Safety: 300ms with no `V` cmd → stop. Mixing: left = lin − ang, right = lin + ang (ang + = turn left).
- Serial protocol (115200): Pi→MCU `V lin ang`, `S`, `A ms`, `Z`, `P text`, `I`, `R`, `B/F 0|1`;
  MCU→Pi `D yaw rate front right status` @50Hz (status 0 ok / 1 calibrating / 2 IMU error) and `L msg` logs.
- **Gotcha:** opening the Nano's serial port toggles DTR and **resets the board**. Output starts ~1.5–2s later.
  The bridge tolerates this. Any test script must wait for `L boot vacuum-fw`.
- 2KB RAM on the Nano: if adding many strings, consider `F("...")` to keep literals in flash.

## Git state

Branch `feature/nano-pinout-motor-test` (commit `f142c14`, pushed, PR not opened yet): Nano pin remap + D7 soft PWM,
motor ramp (`A`), IMU error tolerance, `imu_motor_test.py/.sh`, test_serial DTR reset, devlog `docs/devlog/2026-10-17.md`.
Devlogs go in `docs/devlog/YYYY-MM-DD.md` (Traditional Chinese).

## Next steps / open items

1. Confirm Nano wiring with the user, wire the MPU6050, check `L imu ok` + yaw via `./monitor.sh 5`.
2. Test motors on the Nano (direction per side, PWM deadband, STBY on A0), then the full teleop path (`./ros2_ws/run.sh` + browser).
3. Update top-level `README.md` (still says Nucleo/ACM0; the user had in-progress edits there, so ask before rewriting) and
   `docs/architecture.md` (still says Arduino Uno + L298N/UART).
4. Firmware: HC-SR04 ×2 (alternate triggers, median filter, fill `D` front/right), buzzer/fan (`B`/`F`), maybe heading PID.
5. ROS2: localization (yaw + ultrasonic wall distances), mission state machine (route in architecture.md §3), red-circle vision.
6. Deadlines: midterm teleop 11/13 (rehearsal 11/06), final autonomous 12/11.
