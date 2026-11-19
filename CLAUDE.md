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
- **2026-11-13: Nano died, replaced by an Arduino UNO clone** (CH340, `/dev/ttyUSB0`, by-id `usb-1a86_USB2.0-Serial-if00-port0`). Same chip and same pin numbers, so `config.h` is shared. PlatformIO env `uno` is now the default (`./flash.sh` = uno, `./flash.sh nano` for a Nano). `brltty` was removed (it grabbed the CH340). First boot on UNO: firmware OK, I²C lines pulled high, but the scan finds no device (check SDA/SCL swap, GND).
- **2026-11-20: new Pi + new Arduino UNO, camera-tilt servo added.** MG90S (metal gear, 180°) on **D9**, driven by
  Timer2 CTC ISR (not `Servo.h`: that takes Timer1, which D7's soft PWM uses; so D3/D11 can't `analogWrite` now).
  Serial `C <0..180|-1>`, rate-limited 150°/s, no pulses until the first `C`. Bridge `/camera/tilt_cmd` → `C`,
  resent on connect and `L boot`; web page has presets 巡航/看紙片/看紅點 (keys 1/2/3, R/V ±5°, "存成模式" saves to
  `ros2_ws/camera_presets.json`, gitignored; defaults 90/60/120 are guesses, user must calibrate). Servo pin was
  proposed by the agent (user didn't object); flashing/servo not yet tested on hardware (UNO wasn't plugged in).
  **Not installed yet: IMU and ultrasonics. The vacuum MOSFET is broken, so the fan is always on** (`F` does nothing).
  User said these don't matter for the 11/21 midterm; fix after. Expect `D` status 2 (IMU error) on the page.
- Motor driver: TB6612FNG, 2 DC motors (differential drive). The old docs mention L298N/Uno: outdated.
- Camera: Logitech C310 on the Pi.
- **Nano wiring confirmed by the user (2026-10-17, soldered perfboard)**, see `firmware/src/config.h`:
  PWMA = D6 = right motor, PWMB = D7 = left motor (`MOTOR_SWAP_LR`, swapped 2026-11-05) (D6: Timer0 HW PWM, 976Hz; D7 **no HW PWM** → Timer1 ISR soft PWM, 976Hz; D9/D10 can't be PWM now),
  AIN1/AIN2 = A3/A2, BIN1/BIN2 = A0/A1 (swapped 2026-11-12, left wheel was reversed), STBY = 3.3V, MPU6050 SDA/SCL = A4/A5, vacuum relay (JQC-3FF-S-Z, on NO, `VACUUM_ON_LEVEL` LOW) = **D12** (moved from D8 on 2026-11-13: D8 was shorted to A5/SCL on the perfboard; D8 now unused, left as INPUT). Camera servo = D9 (2026-11-20). 2026-11-06: closing the page turned the vacuum on, proving D8 LOW = running, so the level is now LOW and the label inversion in `showVacuum` is gone. Vacuum log lines from before this fix are inverted. Free: D2–D5, D10–D11.
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
- **2026-11-12: camera drops / Pi resets when motors spin = power.** Kernel log that evening: 5× `Undervoltage detected`,
  C310 `USB disconnect` + enumerate errors -71/-32 (camera gone ~5 min), and 4 boots ending abruptly (hard resets).
  The batteries were low (being charged). Software can't fix this. The Pi needs its own 5V ≥3A buck/supply that motor
  current surges can't pull down (or a separate power bank), with a big cap on VM. `cam.sh` now opens the camera by
  `/dev/v4l/by-id` so it recovers if it re-enumerates as another `/dev/videoN`. Read undervoltage via the
  `rpi_volt` hwmon `in0_lcrit_alarm`, not `get_throttled` (deprecated, dumps a WARNING trace into dmesg on this kernel).
- **2026-11-13: fan (vacuum relay) on → IMU lost, = wiring, not EMI.** With D8 LOW, SCL (A5) reads low 1000/1000
  (SDA stays high) even after `Wire.end()`, and SCL follows D8 within 0–500µs, before the relay contacts can close
  (~10ms). So A5 is electrically tied to the relay control side (bridge/wire between A5 and D8 / relay IN / coil
  driver). User to continuity-check A5↔D8/relay IN with power off. `I` now also prints SDA/SCL low-counts (idle and
  with the MCU's TWI released) to diagnose this kind of thing.
- IMU mounting: only gyro-z is used, and yaw rate is the same anywhere on a rigid body, so an off-center IMU is fine.
  It must be level and rigidly mounted.

## Components and status

| Area | Status | Notes |
|---|---|---|
| `stream/` | ✅ works | C310 → Pi HW H.264 (v4l2h264enc) → MediaMTX → WebRTC `:8889/cam`, RTSP `:8554/cam`. `./stream/start.sh -d` / `stop.sh`. **LAN only**: `webrtcIPsFromInterfacesExcludeList: [tailscale0, docker0]` (over Tailscale the video was throttled); menu/start.sh print the Wi-Fi IP from `stream/lanip.sh` |
| `firmware/` | 🚧 | IMU yaw ✅ (validated on Nucleo), serial protocol ✅ (tested on Nano), motors untested on Nano, vacuum relay `F 0|1` on D8 written but untested on hardware, ultrasonic/buzzer not implemented (`D` reports `-1 -1`; `B` cmd is a stub) |
| `ros2_ws/vacuum_bridge` | ✅ | serial ↔ ROS topics; `/cmd_vel` −1..1 scaled to ±255 `V` cmds at 20Hz; ping every 1s → `/mcu/rtt_ms`; `/vacuum/cmd` → `F`, firmware's `L vacuum on/off` → `/vacuum/state`; `/camera/tilt_cmd` → `C`, `L cam N` → `/camera/tilt` |
| `ros2_ws/vacuum_teleop` | ✅ | web page + WebSocket on `:8080` (camera background, yaw tape, joystick, `teleop_twist_keyboard` key layout held to move, Q/Z or +/− W/X E/C speed, K or space = stop, 0 = zero, button/F = vacuum relay, camera tilt panel: presets 1/2/3, R/V ±5°) |
| `ros2_ws/vacuum_bringup` | ✅ | `teleop.launch.py` |
| localization / mission / vision | ❌ not started | planned packages `vacuum_localization`, `vacuum_mission`, `vacuum_vision` (red circle, 20cm) |

## Commands

```bash
./menu.sh                      # service menu: shows stream/ROS2/camera/Nano/undervoltage status, 1/2 toggle, a/x all on/off
./menu.sh status               # print status once (non-interactive, fine for agents)
./health.sh report             # boots in last 24h: runtime, abrupt power loss?, undervoltage/camera-drop counts
tail logs/health.log           # health monitor events (menu.sh auto-starts `health.sh -d`). At session start, check
                               # logs/health.log for "突然斷電" and tell the user (they asked to be told about power loss)

# Firmware (in firmware/)
./build.sh                     # compile Nano (default env)
./build.sh -e nucleo_l432kc    # compile old Nucleo
./flash.sh                     # build + avrdude upload to Nano (refuses if vacuum-ros2 is running)
./flash.sh nucleo              # copy .bin to NODE_L432KC mass storage
./monitor.sh                   # interactive miniterm (needs a TTY; agents should use `./monitor.sh 5`)
./monitor.sh 5                 # read 5s of output and exit (good for agents)
./imu_motor_test.sh [speed]    # wheels LIFTED: gyro noise/drift/I2C errors with ramp off vs 300ms (speed default 120)
./test.sh [--no-motor]         # automated protocol test (test_serial.py; plain run spins motors, lift wheels): boot msg, 50Hz D, ping, I2C scan, 300ms timeout, unknown cmd, R reboot

# ROS2 (in ros2_ws/)
./run.sh [-d]                  # (-d = detached) colcon build + launch bridge + web teleop; maps Nano/Nucleo serial to /dev/ttyACM0 in container
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
- Motor ramp (trapezoid): `V` sets a target; output slews away from 0 at 255/`MOTOR_ACCEL_MS` (800ms) and toward 0 at 255/`MOTOR_DECEL_MS` (300ms); reversal decels to 0 first. Output is 1/256-PWM fixed point so slow ramps aren't lost. Runtime `A <accel> [decel]` (one arg = both, 0 = off) → `L ramp accel N decel M ms`. Bridge param `accel_ms`/`decel_ms`, `/motor/accel_ms` sets accel (web page −/+), resent after Nano reboot; `/motor/accel_state` latched. `S`/timeout stop instantly (coast).
- IMU errors: single I²C read failures are skipped (`L imu read errors N` per second); lost only after 10 in a row; re-init keeps bias/yaw (calibrates only once, or on `Z`).
- Safety: 300ms with no `V` cmd → stop. Nano watchdog 500ms (`H 0|1` = test hang, boot reports `L reset cause:`); bridge pulses DTR after 3s of silence. Mixing: left = lin − ang, right = lin + ang (ang + = turn left).
- Serial protocol (115200): Pi→MCU `V lin ang`, `S`, `A ms`, `Z`, `P text`, `I`, `R`, `B/F 0|1`, `C deg`;
  MCU→Pi `D yaw rate front right status` @50Hz (status 0 ok / 1 calibrating / 2 IMU error) and `L msg` logs.
- **Gotcha:** opening the Nano's serial port toggles DTR and **resets the board**. Output starts ~1.5–2s later.
  The bridge tolerates this. Any test script must wait for `L boot vacuum-fw`.
- 2KB RAM on the Nano: if adding many strings, consider `F("...")` to keep literals in flash.

## Git state

All work is on `main` (pushed). The user pushes straight to `main` (no PRs) despite the README's branch workflow.
Devlogs go in `docs/devlog/YYYY-MM-DD.md` (Traditional Chinese).

## Next steps / open items

- **After the 11/21 midterm:** replace the vacuum MOSFET (fan stuck on), install IMU + 2× HC-SR04, calibrate the
  camera-tilt presets on the real arm.

0. **In progress (2026-11-13):** user is soldering the vacuum relay off the perfboard: IN → Nano **D12** header, VCC/GND
   straight to Nano 5V/GND headers. Then set `PIN_VACUUM 12` in `config.h` (keep D8 as INPUT), flash, rerun the fan
   test (fan on → `I` must show SCL 0/1000, no `imu lost`). Battery: user to measure per-cell voltages (suspect 3S BMS
   cutting out on overcurrent/unbalanced cell). See `docs/devlog/2026-11-13.md`.
1. Confirm Nano wiring with the user, wire the MPU6050, check `L imu ok` + yaw via `./monitor.sh 5`.
2. Test motors on the Nano (direction per side, PWM deadband, STBY on A0), then the full teleop path (`./ros2_ws/run.sh` + browser).
3. Update top-level `README.md` (still says Nucleo/ACM0; the user had in-progress edits there, so ask before rewriting) and
   `docs/architecture.md` (still says Arduino Uno + L298N/UART).
4. Firmware: HC-SR04 ×2 (alternate triggers, median filter, fill `D` front/right), buzzer (`B`), maybe heading PID.
5. ROS2: localization (yaw + ultrasonic wall distances), mission state machine (route in architecture.md §3), red-circle vision.
6. Deadlines: midterm teleop 11/13 (rehearsal 11/06), final autonomous 12/11.
