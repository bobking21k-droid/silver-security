#!/bin/sh
# SilverOS boot sequence, run once by silver-init before services start.
# With 'quiet' on the kernel command line the output goes to /run/boot.log instead.
grep -qw quiet /proc/cmdline && exec >/run/boot.log 2>&1
chown -R silver:silver /home/silver; chmod 700 /home/silver
echo "  * loopback";   ip link set lo up
echo "  * random seed"; [ -f /var/lib/random-seed ] && cat /var/lib/random-seed > /dev/urandom
echo "  * network: wired (DHCP, background)"
for dev in /sys/class/net/*; do
  n=${dev##*/}; [ "$n" = lo ] && continue
  [ -d "$dev/wireless" ] || [ -d "$dev/phy80211" ] && continue        # Wi-Fi is silver-wifi's job
  ip link set "$n" up
  udhcpc -b -i "$n" -s /etc/udhcpc/default.script >/dev/null 2>&1 &
done
echo "  * network: Wi-Fi (silver-wifi, background)"
/sbin/silver-wifi >/dev/null 2>&1 &
if [ -s /root/.ssh/authorized_keys ] || ls /home/*/.ssh/authorized_keys >/dev/null 2>&1; then
  echo "  * ssh (key-only)"
  [ -f /etc/dropbear/dropbear_ed25519_host_key ] || \
    dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key >/dev/null 2>&1
  dropbear -s -g -r /etc/dropbear/dropbear_ed25519_host_key   # -s no passwords, -g no root password
else
  echo "  * ssh disabled (no authorized_keys)"
fi
