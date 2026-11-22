#!/usr/bin/env python3
"""Security test for the Steam session script (it runs as root): symbolic links planted by the user in
the places it used to write must not let it overwrite root-owned files, and it must leave nothing
owned by root in the user's home. Needs the test disk that holds Steam's launcher package
(work/test/g/steamdeb.img: see the README's Steam notes) and the games layer in the ISO.
Usage: tests/steam-session-security-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir> <steamdeb.img>"""
import sys, os, time, subprocess
here = os.path.dirname(os.path.abspath(__file__))
deb_img = sys.argv[5]
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
_Popen = subprocess.Popen
def P(args, **k):
    return _Popen(list(args) + ["-drive", "file=%s,format=raw,if=none,id=s0,snapshot=on" % deb_img, "-device", "nvme,drive=s0,serial=STEAMDEB"], **k)
subprocess.Popen = P
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60); time.sleep(5)
vm.send("silver"); vm.expect(r"~\$ ")
vm.send("su"); vm.expect("Password:"); vm.send("Admin#Test12"); vm.expect(r"# ")
def run(c, t=90):
    vm.send(c); return strip(vm.expect(r"# ", t))
run("mkdir -p /run/sd && mount -o ro $(findfs LABEL=STEAMDEB) /run/sd && mkdir -p /home/silver/.cache /home/silver/.local/state/silver && cp /run/sd/steam_latest.deb /home/silver/.cache/ && chown -R silver:silver /home/silver/.cache /home/silver/.local")
# root-owned victims, and the user's symbolic links pointing at them
run("echo ORIGINAL > /root/victim1; echo ORIGINAL > /root/victim2; echo ORIGINAL > /root/victim3; chmod 644 /root/victim*; chmod 755 /root")
run("su silver -c 'ln -sf /root/victim1 /home/silver/.local/state/silver/steam-session.log; ln -sf /root/victim2 /home/silver/.local/state/silver/Xorg.log; ln -sf /root/victim3 /home/silver/.Xauthority'")
run("(cd /; timeout 100 bash /usr/lib/silver/steam-session silver open > /tmp/ss.out 2>&1 &) ; sleep 45; ls -la /run/silver-x; ls -la /run/user")
print("   session running; ending it ...")
run("pkill -f steam-x; pkill -x steam; pkill -f ubuntu12_32; pkill Xorg; sleep 8; echo ended")
out = run("echo victims:; cat /root/victim1 /root/victim2 /root/victim3; echo owned-by-root-in-home:; find /home/silver -user root 2>/dev/null | head; echo user-log:; ls -la /home/silver/.local/state/silver; head -c 200 /home/silver/.local/state/silver/steam-session.log")
print(out)
if out.split("\nvictims:")[1].split("\nowned-by-root-in-home:")[0].count("ORIGINAL") != 3: vm.fail("a planted symbolic link let the root script overwrite a root-owned file")
if "owned-by-root-in-home:\n" in out and out.split("\nowned-by-root-in-home:")[1].split("\nuser-log:")[0].strip(): vm.fail("the root script left root-owned files in the user's home")
print("security: planted symbolic links did not work, nothing root-owned left in the home folder")
vm.off()
