#!/bin/sh
# SilverOS boot sequence, run once by silver-init before services start.
# With 'quiet' on the kernel command line the output goes to /run/boot.log instead.
grep -qw quiet /proc/cmdline && exec >/run/boot.log 2>&1
chown -R silver:silver /home/silver; chmod 700 /home/silver
echo "  * loopback";   ip link set lo up
echo "  * random seed"; [ -f /var/lib/random-seed ] && cat /var/lib/random-seed > /dev/urandom
echo "  * games layer (graphics, Bluetooth firmware, Steam's libraries)"
/sbin/silver-games-mount && [ -d /usr/lib/firmware ] && printf /usr/lib/firmware > /sys/module/firmware_class/parameters/path
echo "  * drivers for this computer's Wi-Fi, graphics and Bluetooth (signed modules)"
# load the module for each device present, then keep listening for devices plugged in later
for a in $(cat /sys/bus/pci/devices/*/modalias /sys/bus/usb/devices/*/modalias 2>/dev/null | sort -u); do
  modprobe "$a" 2>/dev/null
done
if [ -x /usr/sbin/udevd ]; then                       # (from the games layer) devices for Xorg and Steam
  /usr/sbin/udevd --daemon && udevadm trigger --action=add && udevadm settle -t 10
else
  uevent mdev >/dev/null 2>&1 &
fi
sleep 1                                   # let new network interfaces and the graphics card settle
if [ -x /usr/bin/dbus-daemon ]; then                  # the system message bus, and Bluetooth on it
  echo "  * Bluetooth"
  mkdir -p /run/dbus /var/lib/dbus /var/lib/bluetooth; chmod 700 /var/lib/bluetooth
  dbus-uuidgen --ensure 2>/dev/null; [ -e /etc/machine-id ] || cp /var/lib/dbus/machine-id /etc/machine-id 2>/dev/null   # (Steam wants one)
  dbus-daemon --system --fork
  for r in /sys/class/rfkill/rfkill*; do [ "$(cat "$r/type" 2>/dev/null)" = bluetooth ] && echo 0 > "$r/soft"; done
  for b in /usr/libexec/bluetooth/bluetoothd /usr/lib/bluetooth/bluetoothd; do [ -x "$b" ] && { "$b" >/dev/null 2>&1 & break; }; done
fi
echo "  * firewall (nothing gets in unless you asked)"
if [ -x /sbin/nft ] && /sbin/nft -f /etc/nftables.conf 2>/run/nft.err; then :; else echo "    !! the firewall did not load: $(cat /run/nft.err 2>/dev/null | head -1)"; fi
echo "  * network: wired (DHCP, background)"
for dev in /sys/class/net/*; do
  n=${dev##*/}; [ "$n" = lo ] && continue
  [ -d "$dev/wireless" ] || [ -d "$dev/phy80211" ] && continue        # Wi-Fi is silver-wifi's job
  ip link set "$n" up
  udhcpc -b -i "$n" -s /etc/udhcpc/default.script >/dev/null 2>&1 &
done
# the clock: set from the internet once the network is up (see silver-time)
/sbin/silver-time >/dev/null 2>&1 &
echo "  * network: Wi-Fi (silver-wifi, background)"
/sbin/silver-wifi >/dev/null 2>&1 &
if [ -s /root/.ssh/authorized_keys ] || ls /home/*/.ssh/authorized_keys >/dev/null 2>&1; then
  echo "  * ssh (key-only)"
  [ -f /etc/dropbear/dropbear_ed25519_host_key ] || \
    dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key >/dev/null 2>&1
  dropbear -s -g -r /etc/dropbear/dropbear_ed25519_host_key   # -s no passwords, -g no root password
  /sbin/nft add rule inet silver input tcp dport 22 ct state new accept 2>/dev/null   # the one door we open, because you asked
else
  echo "  * ssh disabled (no authorized_keys)"
fi
