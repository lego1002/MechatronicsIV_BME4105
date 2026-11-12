#!/bin/bash
# 健康監控：記錄低電壓、串流斷線、攝影機 USB 掉線，並偵測「上次是不是突然斷電」
#   ./health.sh -d      背景執行（menu.sh 會自動幫你開）
#   ./health.sh stop    停止
#   ./health.sh report  看今天每次開機撐多久、怎麼結束、低電壓/攝影機掉線次數
#   tail -f logs/health.log
# 只記「狀態改變」的事件 + 每 30 秒一行心跳；每行寫完就 sync，突然斷電也留得住最後狀態。
cd "$(dirname "$0")"
LOG=logs/health.log
PIDF=logs/health.pid
mkdir -p logs

running() { [ -f $PIDF ] && kill -0 "$(cat $PIDF)" 2>/dev/null; }

# 上一次開機是正常關機還是突然斷電：正常關機時 journal 最後會有 Journal stopped / systemd-shutdown
prev_boot_ended() {
  journalctl -b "$1" --no-pager -n 60 2>/dev/null \
    | grep -q -E "Journal stopped|Reached target.*(Power-Off|Reboot|Halt)|systemd-shutdown" && echo clean || echo abrupt
}

report() {
  echo "最近 24 小時的開機紀錄（突然結束 = 斷電/硬重置）："
  # list-boots 欄位：idx id 週 日期 時間 時區—週 日期 時間 時區（不用 journal 第一行：Pi 沒 RTC，開機初期時間是舊的）
  journalctl --list-boots --no-pager 2>/dev/null | sed 's/—/ /' | awk '{print $1, $4" "$5, $8" "$9}' |
  while read -r b sd st ed et; do
    s=$(date -d "$sd $st" +%s); e=$(date -d "$ed $et" +%s)
    [ $(( $(date +%s) - e )) -gt 86400 ] && continue
    uv=$(journalctl -k -b "$b" --no-pager -q 2>/dev/null | grep -c 'Undervoltage detected')
    cam=$(journalctl -k -b "$b" --no-pager -q 2>/dev/null | grep -c 'usb 1-1.1: USB disconnect')
    if [ "$b" = 0 ]; then how="（現在這次）"; else how=$([ "$(prev_boot_ended "$b")" = clean ] && echo 正常關機 || echo "⚠ 突然斷電"); fi
    printf "  %s → %s  撐了 %3d 分  低電壓 %d 次  攝影機掉線 %d 次  %s\n" \
      "$(date -d @"$s" +%H:%M)" "$(date -d @"$e" +%H:%M)" $(( (e - s) / 60 )) "$uv" "$cam" "$how"
  done
}

case "$1" in
  stop)   running && kill "$(cat $PIDF)" && rm -f $PIDF && echo "健康監控已停止"; exit ;;
  report) report; exit ;;
  -d)     running && { echo "健康監控已經在跑"; exit 0; }
          setsid "$0" run >/dev/null 2>&1 < /dev/null &
          echo "健康監控已在背景啟動，log：$LOG"; exit ;;
  run)    ;;
  *)      running && { echo "健康監控已經在跑（tail -f $LOG）"; exit 0; } ;;
esac
echo $$ > $PIDF

log() { echo "$(date '+%F %T') $*" >> $LOG; sync -d $LOG 2>/dev/null; }

VOLT=$(for h in /sys/class/hwmon/hwmon*; do [ "$(cat $h/name)" = rpi_volt ] && echo $h/in0_lcrit_alarm; done)

# 啟動時：上次開機是怎麼結束的？
BOOT=$(cat /proc/sys/kernel/random/boot_id)
if [ "$(cat logs/health.boot 2>/dev/null)" != "$BOOT" ]; then
  echo "$BOOT" > logs/health.boot
  if [ "$(prev_boot_ended -1)" = abrupt ]; then
    last=$(grep -v '^$' $LOG 2>/dev/null | tail -1)
    echo "$BOOT" > logs/health.abrupt   # menu.sh 用：這次開機前是突然斷電
    log "⚠ 偵測到上次突然斷電（電池沒電或電壓掉下去）。斷電前最後一筆：${last:-（無）}"
  else
    log "上次是正常關機"
  fi
fi
log "監控啟動 (pid $$)"

prev=; n=0
while true; do
  uv=$(cat "$VOLT" 2>/dev/null)
  mtx=$(pgrep -x mediamtx >/dev/null && echo 1 || echo 0)
  ready=$(curl -s -m 1 127.0.0.1:9997/v3/paths/get/cam 2>/dev/null | grep -q '"ready":true' && echo 1 || echo 0)
  readers=$(curl -s -m 1 127.0.0.1:9997/v3/paths/get/cam 2>/dev/null | grep -o '"type":"webRTCSession"' | wc -l)
  cam=$(ls /dev/v4l/by-id/usb-046d_081b_*-video-index0 >/dev/null 2>&1 && echo 1 || echo 0)
  ros=$(docker ps -q -f name=^vacuum-ros2$ 2>/dev/null | grep -q . && echo 1 || echo 0)
  temp=$(( $(cat /sys/class/thermal/thermal_zone0/temp) / 1000 ))
  state="低電壓=$uv 攝影機=$cam 串流=$mtx 畫面=$ready 觀看=$readers ros2=$ros"
  if [ -n "$prev" ]; then
    set -- $prev; p_uv=${1#*=}; p_cam=${2#*=}; p_mtx=${3#*=}; p_ready=${4#*=}; p_rd=${5#*=}
    [ "$uv" = 1 ] && [ "$p_uv" != 1 ] && log "⚠ 低電壓！（電池快沒電或馬達/吸塵器瞬間抽電）$state"
    [ "$uv" = 0 ] && [ "$p_uv" = 1 ] && log "電壓恢復 $state"
    [ "$cam" = 0 ] && [ "$p_cam" = 1 ] && log "⚠ 攝影機 USB 掉了 $state"
    [ "$cam" = 1 ] && [ "$p_cam" = 0 ] && log "攝影機回來了 $state"
    [ "$mtx" = 0 ] && [ "$p_mtx" = 1 ] && log "串流 MediaMTX 停了 $state"
    [ "$mtx" = 1 ] && [ "$p_mtx" = 0 ] && log "串流 MediaMTX 啟動 $state"
    [ "$mtx" = 1 ] && [ "$ready" = 0 ] && [ "$p_ready" = 1 ] && log "⚠ 串流沒畫面了 $state"
    [ "$ready" = 1 ] && [ "$p_ready" = 0 ] && log "串流有畫面 $state"
    [ "$readers" -lt "$p_rd" ] && log "觀看者斷線（$p_rd → $readers）$state"
  fi
  prev=$state
  (( n++ % 15 == 0 )) && log "心跳 $state CPU=${temp}°C 開機=$(awk '{printf "%d分", $1/60}' /proc/uptime)"
  sleep 2
done
