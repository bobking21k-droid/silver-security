#!/bin/sh
# SilverOS boot sequence, run once by silver-init before services start.
chown -R silver:silver /home/silver; chmod 700 /home/silver
echo "  * loopback";   ip link set lo up
echo "  * random seed"; [ -f /var/lib/random-seed ] && cat /var/lib/random-seed > /dev/urandom
echo "  * network (DHCP, background)"
for dev in /sys/class/net/*; do
  n=${dev##*/}; [ "$n" = lo ] && continue
  ip link set "$n" up
  udhcpc -b -q -i "$n" -s /etc/udhcpc/default.script >/dev/null 2>&1 &
done
if [ -s /root/.ssh/authorized_keys ] || ls /home/*/.ssh/authorized_keys >/dev/null 2>&1; then
  echo "  * ssh (key-only)"
  [ -f /etc/dropbear/dropbear_ed25519_host_key ] || \
    dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key >/dev/null 2>&1
  dropbear -s -g -r /etc/dropbear/dropbear_ed25519_host_key   # -s no passwords, -g no root password
else
  echo "  * ssh disabled (no authorized_keys)"
fi
