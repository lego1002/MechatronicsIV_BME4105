#!/bin/bash
# USB 攝影機 -> Pi 4 硬體 H.264 編碼 -> RTP/UDP -> MediaMTX (127.0.0.1:5004)
# 由 MediaMTX 的 runOnInit 啟動；本腳本結束後 MediaMTX 會自動重新啟動它。
# 可用環境變數調整：W H FPS BITRATE(bps) GOP(幾幀一個關鍵幀) DEV
DEV=${DEV:-/dev/video0}
W=${W:-640}; H=${H:-480}; FPS=${FPS:-30}
BITRATE=${BITRATE:-1000000}
GOP=${GOP:-15}

gst-launch-1.0 -q \
  v4l2src device="$DEV" do-timestamp=true extra-controls="c,exposure_auto_priority=0" \
  ! "video/x-raw,format=YUY2,width=$W,height=$H,framerate=$FPS/1" \
  ! queue max-size-buffers=1 leaky=downstream \
  ! videoconvert ! video/x-raw,format=I420 \
  ! v4l2h264enc extra-controls="controls,video_bitrate=$BITRATE,video_bitrate_mode=1,h264_i_frame_period=$GOP,repeat_sequence_header=1" \
  ! "video/x-h264,level=(string)4,profile=(string)constrained-baseline" \
  ! rtph264pay pt=96 config-interval=-1 mtu=1200 \
  ! udpsink host=127.0.0.1 port=5004 sync=false async=false &
GST=$!
trap 'kill -9 $GST 2>/dev/null' EXIT TERM INT

# 看門狗：gst 沒當掉但畫面卡住（攝影機/編碼器卡死）時，砍掉讓 MediaMTX 重啟
sleep 5
last=-1
while kill -0 $GST 2>/dev/null; do
  now=$(curl -s -m 1 127.0.0.1:9997/v3/paths/get/cam | grep -o '"inboundBytes":[0-9]*' | cut -d: -f2)
  if [ -n "$now" ] && [ "$now" = "$last" ]; then
    echo "cam.sh: 3 秒沒有新畫面，重新啟動攝影機" >&2
    exit 1
  fi
  last=$now
  sleep 3
done
