#!/usr/bin/env python3
"""Boot time in QEMU: seconds from power-on to the first-boot prompt and to the login prompt (and the
memory in use). Usage: tests/boot-time.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
t0 = time.time(); vm = VM(with_iso=False)
vm.expect("login:", 200); t1 = t2 = time.time() - t0
time.sleep(20)
vm.send("silver"); vm.expect(r"~\$ ")
vm.send("grep -E 'MemTotal|MemAvailable' /proc/meminfo; ps -o rss,comm | sort -rn | head -6"); print(strip(vm.expect(r"~\$ ")))
print(f"BOOT: login prompt after {t2:.0f}s from power-on (firmware included), installed system")
vm.off()
