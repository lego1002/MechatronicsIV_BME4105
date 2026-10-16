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
- **Nano wiring is NOT confirmed.** The last agent chose these pins (`firmware/src/config.h`, AVR section); verify with the user:
  MPU6050 SDA/SCL = A4/A5, PWMA/PWMB = D9/D10 (Timer1 ~31kHz), AIN1/AIN2 = D7/D8, BIN1/BIN2 = D4/D5, STBY = A0.
  Reserved for later: D2/D3, D11/D12, A1–A3 (ultrasonic, buzzer, vacuum fan).
- At last test **no IMU was detected** on the Nano (I²C scan empty, status=2), so it wasn't wired yet. Motors were not connected/tested.

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
./test.sh                      # automated protocol test (test_serial.py): boot msg, 50Hz D, ping, I2C scan, 300ms timeout, unknown cmd, R reboot

# ROS2 (in ros2_ws/)
./run.sh                       # colcon build + launch bridge + web teleop; maps Nano/Nucleo serial to /dev/ttyACM0 in container
./run.sh shell                 # shell in the (running) container
docker stop vacuum-ros2
```

The agent can build, flash and test the firmware on its own via these scripts. Last run: Nano build has 0 warnings
(11.2KB flash, 952B RAM), both envs build, `./test.sh` passes 8/8.

## Firmware design notes (`firmware/src/main.cpp`, `config.h`)

- One source file for both boards, platform bits under `#if defined(ARDUINO_ARCH_AVR)`:
  - AVR: Timer1 prescaler 1 → ~31kHz PWM on D9/D10 (doesn't touch Timer0/millis); `Wire.setWireTimeout(3000,true)`
    (otherwise a loose I²C wire hangs the loop); `R` reset via watchdog (`wdt_enable(WDTO_15MS)`; MCUSR/wdt cleared in setup).
  - STM32: solder-bridge short check (SB16/SB18 tie A4/A5 to D4/D5), TIM2 soft-PWM for pins without timers, `NVIC_SystemReset`.
- IMU: MPU6050 gyro z @200Hz, ±500dps, DLPF 42Hz; 2s still calibration at boot / on `Z`; slow bias tracking when motors
  idle >1s; auto re-init every 1s if IMU lost.
- Safety: 300ms with no `V` cmd → stop. Mixing: left = lin − ang, right = lin + ang (ang + = turn left).
- Serial protocol (115200): Pi→MCU `V lin ang`, `S`, `Z`, `P text`, `I`, `R`, `B/F 0|1`;
  MCU→Pi `D yaw rate front right status` @50Hz (status 0 ok / 1 calibrating / 2 IMU error) and `L msg` logs.
- **Gotcha:** opening the Nano's serial port toggles DTR and **resets the board**. Output starts ~1.5–2s later.
  The bridge tolerates this. Any test script must wait for `L boot vacuum-fw`.
- 2KB RAM on the Nano: if adding many strings, consider `F("...")` to keep literals in flash.

## Uncommitted work (git status at handoff)

Only commit `75eaee1` exists. All of the following is uncommitted: `docker/`, `firmware/` (src, platformio.ini, build/flash/monitor/port/test scripts,
test_serial.py), `ros2_ws/run.sh`, `ros2_ws/src/`, edits to `.gitignore`, `README.md`, `firmware/README.md`, `ros2_ws/README.md`.

## Next steps / open items

1. Confirm Nano wiring with the user, wire the MPU6050, check `L imu ok` + yaw via `./monitor.sh 5`.
2. Test motors on the Nano (direction per side, PWM deadband, STBY on A0), then the full teleop path (`./ros2_ws/run.sh` + browser).
3. Update top-level `README.md` (still says Nucleo/ACM0; the user had in-progress edits there, so ask before rewriting) and
   `docs/architecture.md` (still says Arduino Uno + L298N/UART).
4. Firmware: HC-SR04 ×2 (alternate triggers, median filter, fill `D` front/right), buzzer/fan (`B`/`F`), maybe heading PID.
5. ROS2: localization (yaw + ultrasonic wall distances), mission state machine (route in architecture.md §3), red-circle vision.
6. Deadlines: midterm teleop 11/13 (rehearsal 11/06), final autonomous 12/11.
