#!/bin/bash
# 印出 Pi 在區網（Wi-Fi）上的 IPv4。刻意不用 Tailscale（100.x）：走 Tailscale 影像會被限速、卡頓
ip=$(ip -4 -o addr show wlan0 2>/dev/null | awk '{split($4,a,"/"); print a[1]; exit}')
[ -z "$ip" ] && ip=$(ip -4 -o addr show eth0 2>/dev/null | awk '{split($4,a,"/"); print a[1]; exit}')
# 都沒有就從 hostname -I 挑第一個不是 Tailscale（100.64/10）也不是 docker（172.17）的
[ -z "$ip" ] && ip=$(hostname -I | tr ' ' '\n' | grep -vE '^(100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\.|172\.17\.)' | grep -E '^[0-9.]+$' | head -1)
echo "$ip"
