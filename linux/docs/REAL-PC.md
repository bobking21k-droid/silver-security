# First time on a real PC: a checklist

SilverOS has only been run in a virtual PC so far. This is what to try on real hardware, in order,
and what to look at if something doesn't work. None of it can hurt Windows if you only boot the
USB stick (the installer is a separate, deliberate step).

## Before you start
- Write the ISO to a USB stick (see the README), turn off Windows **Fast Startup**, and make a
  note of your BitLocker recovery key if BitLocker is on (https://aka.ms/myrecoverykey).
- **Secure Boot:** SilverOS isn't signed yet, so turn it off in the firmware setup to boot it.
  (With BitLocker on, Windows may ask for its recovery key once after that change.)
- Start from the firmware's boot menu and pick the entry that starts with `UEFI:` for the stick.

## Try these from the live USB
| Thing | What to try | If it fails |
|---|---|---|
| Screen | Home screen shows, text is sharp | Settings > Display: ENTER cycles the interface size |
| Keyboard | Type in the Settings > Keyboard "try it" box | Pick your layout there |
| Touchpad / mouse | Move, tap, scroll on the home screen | `su -c dmesg \| grep -i -E 'i2c\|hid\|psmouse'` in the Terminal |
| Wi-Fi | Settings > Network, join your network | `su -c dmesg \| grep -i -E 'firmware\|iwl\|rtw\|mt79'` (missing firmware is the usual cause) |
| Wired network | Plug in a cable; Network shows an address | `ip link`, `su -c dmesg \| grep -i eth` |
| Sound | Settings > Sound effects on; the intro plays | Newer Intel/AMD laptops need "SOF" firmware that is **not included yet**: no sound there |
| Brightness / volume | Settings sliders, or the keys | Note which laptop; some need a vendor driver |
| Bluetooth | Settings > Bluetooth, scan | Untested even in the virtual PC |
| Power button | Press it: a "Shut down?" panel appears | |
| Lid | Close it: the screen goes dark; open: it returns | (it does not suspend) |
| Battery | Percent shows on the home screen | `ls /sys/class/power_supply` |
| Windows disk | Files app shows only your SilverOS home | (Windows partitions are never mounted) |

## Installing next to Windows
1. In Windows: Disk Management > shrink C: and leave the space **Unallocated** (65 GB or more if
   you want Steam games).
2. Boot the stick, Settings > **Install SilverOS**, read the plan, press Enter.
3. Restart without the stick: you get the SilverOS | Windows chooser.
4. First start: the welcome walks through keyboard, time zone and Wi-Fi.

## Sending me a useful report
Open the Terminal on the home screen and run:

    su -c dmesg > /tmp/dmesg.txt; lspci -nn > /tmp/pci.txt; lsusb > /tmp/usb.txt; ls /sys/class/net /sys/class/input /sys/class/drm

and tell me the laptop/PC model, what worked and what didn't. The hardware ids in `pci.txt` and
`usb.txt` are what drivers are chosen by.
(dmesg needs the admin password: su -c dmesg.)
