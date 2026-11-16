#!/usr/bin/env python3
"""Firewall and time-zone test in QEMU: the ruleset is loaded; data sent from outside does NOT reach a
listening program until its port is opened; a saved time zone is honoured by login shells.
Usage: tests/firewall-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time, subprocess, socket as sk
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
import socket as _sk
class _S(_sk.socket):
    def connect(self, addr):
        for i in range(60):
            try: return super().connect(addr)
            except (FileNotFoundError, ConnectionRefusedError): time.sleep(0.5)
        return super().connect(addr)
_Real = _sk.socket
_sk.socket = _S
_Popen = subprocess.Popen
def P(args, **k):
    args = ["user,model=e1000,hostfwd=tcp:127.0.0.1:18080-:8080" if a == "user,model=e1000" else a for a in args] + ["-rtc", "base=2001-06-01T12:00:00"]
    return _Popen(args, **k)
subprocess.Popen = P
def say(*a): print(*a, flush=True)
def host_connect():
    c = _Real(_sk.AF_INET, _sk.SOCK_STREAM); c.settimeout(5)
    try:
        c.connect(("127.0.0.1", 18080)); c.sendall(b"hi\n"); c.close(); return True
    except Exception as e:
        return False
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60); vm.send("silver"); vm.expect(r"~\$ ")
vm.send("su"); vm.expect("Password:"); vm.send("Admin#Test12"); vm.expect(r"# ")
def run(c, t=60):
    vm.send(c); say(strip(vm.expect(r"# ", t)))
run("grep -i firewall /run/boot.log; nft list ruleset | head -14")
# the firewall: a listener in the guest; data sent from outside must NOT arrive until the port is opened
run("(nc -l -p 8080 > /run/nc.out 2>&1 &); sleep 1; netstat -ltn | grep 8080")
c = _Real(_sk.AF_INET, _sk.SOCK_STREAM); c.settimeout(5); c.connect(("127.0.0.1", 18080)); c.sendall(b"HELLO-FROM-OUTSIDE\n")
time.sleep(4)
run("echo before:; cat /run/nc.out; echo '(end)'")
run("nft add rule inet silver input tcp dport 8080 accept; echo opened")
time.sleep(12)
run("echo after:; cat /run/nc.out; echo '(end)'")
c.close()
# the time zone file is honoured by login shells
run("mkdir -p /home/silver/.config/silver; echo 'JST-9' > /home/silver/.config/silver/tz; chown -R silver:silver /home/silver/.config; TZ=UTC date; su - silver -c 'date'")
vm.off()
