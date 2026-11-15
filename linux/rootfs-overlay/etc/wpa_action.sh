#!/bin/sh
# Run by wpa_cli when Wi-Fi connects or drops: get (or let go of) an address with DHCP.
n=$1; pid=/run/udhcpc.$n.pid
case "$2" in
  CONNECTED)
    [ -f "$pid" ] && kill "$(cat "$pid")" 2>/dev/null
    udhcpc -b -i "$n" -p "$pid" -s /etc/udhcpc/default.script >/dev/null 2>&1 ;;
  DISCONNECTED)
    [ -f "$pid" ] && kill "$(cat "$pid")" 2>/dev/null; rm -f "$pid"
    ip addr flush dev "$n" 2>/dev/null ;;
esac
