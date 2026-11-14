#!/usr/bin/env python3
# Boots the ISO in QEMU over serial and checks first-boot setup, login policy,
# networking and hardening.  Usage: tests/boot-test.py work/out/<image>.iso
import subprocess, time, re, sys
iso = sys.argv[1]
p = subprocess.Popen(["qemu-system-x86_64","-m","1024","-smp","2","-cdrom",iso,"-boot","d",
    "-nic","user,model=e1000","-display","none","-serial","stdio"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
import os; os.set_blocking(p.stdout.fileno(), False)
buf = b""
def expect(pat, t=60):
    global buf
    end = time.time()+t
    while time.time() < end:
        try: d = p.stdout.read()
        except Exception: d = None
        if d: buf += d
        m = re.search(pat.encode(), buf)
        if m: out = buf[:m.end()]; buf = buf[m.end():]; return out
        time.sleep(0.2)
    print("TIMEOUT waiting for", pat); print(buf.decode(errors="replace")[-1500:]); p.kill(); sys.exit(1)
def send(s): p.stdin.write(s.encode()+b"\r"); p.stdin.flush(); time.sleep(0.5)
expect("Press Enter"); send("");
expect("Password for your user"); expect("New password:"); send("Silver#Test1"); expect("Retype"); send("Silver#Test1")
expect("Password for 'root'"); expect("New password:"); send("Root#Test12"); expect("Retype"); send("Root#Test12")
expect("login:"); send("root"); expect("Login incorrect", 15); print("direct root login refused: OK")
expect("login:"); send("silver"); expect("Password:"); send("Silver#Test1"); expect(r"~\$ ")
time.sleep(1)
for c in ["cat /etc/os-release | head -1","id","uname -r","sleep 3; ip -4 addr show eth0 | grep inet","cat /proc/sys/kernel/kptr_restrict /proc/sys/kernel/yama/ptrace_scope","ls /proc | grep -c '^[0-9]'","ls -l /dev/mem 2>&1","cat /sys/kernel/security/lockdown"]:
    send(c); print(expect(r"~\$ ", 15).decode(errors="replace").replace("\r",""))
buf=b""; send("su -c 'id' root"); expect("Password:"); send("Root#Test12"); expect("uid=0", 15); print("su OK")
p.kill()
