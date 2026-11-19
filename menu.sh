#!/bin/bash
# 車子服務選單：一眼看出哪些服務開著，按數字開/關
#   ./menu.sh          互動選單
#   ./menu.sh status   只印一次狀態就結束（給 ssh 一行看）
cd "$(dirname "$0")"
ROOT=$PWD

G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'
on()  { echo "${G}● 開${N}"; }
off() { echo "${R}○ 關${N}"; }

# 一律顯示區網（Wi-Fi）IP，不用 Tailscale：走 Tailscale 影像會被限速卡頓
IP=$(./stream/lanip.sh)
SSID=$(iwgetid -r 2>/dev/null)

stream_up() { pgrep -x mediamtx >/dev/null; }
ros_up()    { docker ps -q -f name=^vacuum-ros2$ 2>/dev/null | grep -q .; }
cam_dev()   { ls /dev/v4l/by-id/usb-046d_081b_*-video-index0 2>/dev/null | head -1; }
mcu_dev()   { ls /dev/serial/by-id/usb-FTDI_* /dev/serial/by-id/usb-1a86_* /dev/serial/by-id/usb-Arduino* /dev/serial/by-id/usb-STMicroelectronics_* 2>/dev/null | head -1; }
# 串流有沒有真的在出畫面（MediaMTX API 的 ready 欄位）
stream_ready() { curl -s -m 1 127.0.0.1:9997/v3/paths/get/cam 2>/dev/null | grep -q '"ready":true'; }

# 電源：rpi_volt hwmon（不要讀 get_throttled，那個會在 dmesg 噴 deprecated 警告）
power_line() {
  local alarm hw n
  for hw in /sys/class/hwmon/hwmon*; do
    [ "$(cat "$hw/name" 2>/dev/null)" = rpi_volt ] && alarm=$(cat "$hw/in0_lcrit_alarm" 2>/dev/null)
  done
  n=$(journalctl -k -b 0 --no-pager 2>/dev/null | grep -c 'Undervoltage detected')
  if [ "$alarm" = 1 ]; then echo "${R}${B}⚠ 現在電壓不足！${N}（開機後 $n 次）"
  elif [ "$n" -gt 0 ]; then echo "${Y}現在正常，但開機後低電壓 $n 次${N}"
  else echo "${G}正常${N}（開機後 0 次低電壓）"; fi
}

status() {
  local s r cam mcu temp
  stream_up && s=$(on) || s=$(off)
  ros_up && r=$(on) || r=$(off)
  stream_up && ! stream_ready && s="$s ${Y}（等攝影機畫面…）${N}"
  cam=$(cam_dev); [ -n "$cam" ] && cam="${G}已接上${N}" || cam="${R}找不到（USB 掉了？）${N}"
  mcu=$(mcu_dev); [ -n "$mcu" ] && mcu="${G}已接上${N} $(readlink -f "$mcu")" || mcu="${R}找不到${N}"
  temp=$(awk '{printf "%.0f°C", $1/1000}' /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
  echo "${B}══ 吸塵車 $(hostname)  $IP  更新於 $(date +%H:%M:%S) ══${N}"
  echo "  1) 影像串流 (MediaMTX)       $s"
  echo "  2) ROS2 bridge + 網頁遙控     $r"
  echo
  echo "  攝影機 C310   $cam"
  echo "  Arduino Nano  $mcu"
  echo "  電源          $(power_line)   CPU $temp   開機 $(uptime -p | sed 's/^up //')"
  [ "$(cat logs/health.abrupt 2>/dev/null)" = "$(cat /proc/sys/kernel/random/boot_id)" ] && \
    echo "  ${R}${B}⚠ 這次開機前是突然斷電（電池沒電？）${N}，按 h 看紀錄"
  echo
  echo "  ${B}電腦請連 Wi-Fi「${SSID:-?}」，打開：${N}"
  if [ -z "$IP" ]; then
    echo "  ${R}找不到區網 IP（Wi-Fi 沒連上？）${N}"
  else
    ros_up && echo "  ${G}${B}遙控   http://$IP:8080${N}   密碼 $(cat ros2_ws/teleop_password 2>/dev/null)（影像已內嵌）" \
           || echo "  遙控   http://$IP:8080   ${R}（ROS2 沒開，按 2）${N}"
    stream_up && echo "  串流   http://$IP:8889/cam   （只看影像）" \
              || echo "  串流   http://$IP:8889/cam   ${R}（串流沒開，按 1）${N}"
    echo "  ${Y}不要用 Tailscale 的 100.x 網址，影像會卡${N}"
  fi
}

toggle_stream() {
  if stream_up; then ./stream/stop.sh; else ./stream/start.sh -d; fi
}
toggle_ros() {
  if ros_up; then
    echo "關閉 vacuum-ros2…"; docker stop vacuum-ros2 >/dev/null && echo "已關閉"
  else
    ./ros2_ws/run.sh -d >/dev/null && echo "vacuum-ros2 已在背景啟動（第一次要 colcon build，約 10–30 秒後網頁才連得上）"
  fi
}

[ "$1" = status ] && { status; exit; }

# 健康監控（低電壓、串流斷線、突然斷電）跟著選單自動開，選單關掉它也繼續跑
./health.sh -d >/dev/null

# 只在狀態有變（服務開關、攝影機/Nano 插拔、IP、低電壓）或按了鍵才重畫，
# 不然一直 clear 會讓人選不到/點不了網址
snapshot() {
  IP=$(./stream/lanip.sh); SSID=$(iwgetid -r 2>/dev/null)
  echo "$(stream_up && echo 1)$(stream_ready && echo r)$(ros_up && echo 1) $(cam_dev) $(mcu_dev) $IP $SSID $(power_line)"
}
last=
while true; do
  last=$(snapshot)
  clear
  status
  echo
  echo "${B}  1/2${N} 開關服務   ${B}a${N} 全開   ${B}x${N} 全關   ${B}l${N} ROS2 log   ${B}s${N} 串流 log   ${B}m${N} Nano 輸出 5 秒   ${B}h${N} 健康紀錄"
  echo "  ${B}Enter${N} 重新整理（狀態有變才會自動更新）   ${B}q${N} 離開（服務會繼續跑）"
  # 每 3 秒偷看一次狀態，有變才跳出去重畫
  k=
  while ! read -rsn1 -t 3 k; do [ "$(snapshot)" != "$last" ] && break; done
  [ -z "$k" ] && continue
  echo
  case "$k" in
    1) toggle_stream ;;
    2) toggle_ros ;;
    a) stream_up || ./stream/start.sh -d; ros_up || toggle_ros ;;
    x) stream_up && ./stream/stop.sh; ros_up && toggle_ros ;;
    l) if ros_up; then echo "（Ctrl+C 回選單）"; trap : INT; docker logs -f --tail 40 vacuum-ros2; trap - INT
       else echo "ROS2 沒在跑"; fi ;;
    s) echo "（Ctrl+C 回選單）"; trap : INT; tail -n 40 -f stream/mtx.log; trap - INT ;;
    m) if ros_up; then echo "ROS2 正在用序列埠，先按 2 關掉它"
       else ./firmware/monitor.sh 5; fi ;;
    h) ./health.sh report; echo; tail -n 15 logs/health.log ;;
    q) exit 0 ;;
    *) continue ;;
  esac
  read -rsn1 -t 5 -p "（按任意鍵回選單）" _
done
