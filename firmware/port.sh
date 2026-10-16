# 找 Arduino Nano 的序列埠（FT232R / CH340），給 flash.sh、monitor.sh 用
find_port() {
  local d
  for d in /dev/serial/by-id/usb-FTDI_* /dev/serial/by-id/usb-1a86_* /dev/ttyUSB0; do
    [ -e "$d" ] && { readlink -f "$d"; return; }
  done
}
