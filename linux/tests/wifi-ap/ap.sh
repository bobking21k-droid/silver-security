#!/bin/sh
# Test only: turn the second simulated radio (mac80211_hwsim) into a WPA2 access point,
# "SilverTest", with a DHCP server, so the Network page can be tried for real in QEMU.
D=$(dirname "$0")
echo wlan1 > /run/silver-wifi.skip                      # silver-wifi: leave this one alone
for f in /run/wpa_supplicant.wlan1.pid; do [ -f "$f" ] && kill "$(cat "$f")"; done
pkill -f 'wpa_cli.*wlan1' 2>/dev/null; sleep 1
ip link set wlan1 up; ip addr add 10.42.0.1/24 dev wlan1
# the pretend access point is on this same computer, so its DHCP server sees "incoming" requests: let that
# one test interface through the firewall (a real SilverOS is never a DHCP server)
/sbin/nft add rule inet silver input iifname "wlan1" accept 2>/dev/null
"$D/hostapd" -B "$D/hostapd.conf" && echo "test AP up"
touch /run/udhcpd.leases; udhcpd "$D/udhcpd.conf" && echo "test DHCP up"
