# SilverOS security review (2026-11-16)

What was looked at, what was found, what was fixed, what is left. Not a professional audit.

## Fixed
- **Steam session wrote as root into the user's folders.** A user could plant a symbolic link
  (for example `~/.local/state/silver/steam-session.log` -> a root file) and the root session script
  would overwrite the target. Now root writes only under `/run/silver-x`; everything in the user's
  home (unpacking the launcher, publishing logs) is done as the user via `su`. The script refuses
  uid < 1000. Test: `tests/steam-session-security-test.py` plants three links and checks that no
  root file changed and no root-owned file appears in the home folder.

## Checked and fine
- Only one setuid file: `/bin/busybox` (for `su`). Nothing else is setuid/setgid.
- Sysctls: kptr_restrict 2, dmesg_restrict, ptrace_scope 2, unprivileged BPF off, perf paranoid 3,
  sysrq off, protected symlinks/hardlinks/fifos/regular, suid_dumpable 0, rp_filter, no redirects.
- Firewall: input policy drop, only established/loopback (+ DHCP replies); no listening services.
- Greeter split: the part that draws and reads keys runs unprivileged; the root half accepts only
  fixed one-letter requests, and line requests are validated (`V`/`L` numbers clamped, `K` layout
  ids letters/`-` only and the file must exist, `G` digits only, `N` only when installed and Windows
  is present).
- Kernel modules are signed; Windows partitions are never mounted writable by the installer.

## Trade-offs made on purpose
- **32-bit emulation (IA32) is on**: Steam's client is 32-bit.
- **User namespaces on**: needed by the games sandbox (bwrap) and Steam.
- **The Steam session runs Xorg as root**: needed for GPU/KMS access without a login manager.
  The Steam client itself runs as the user.

## Done: kernel tightened
`DEBUG_FS`, `KALLSYMS_ALL`, `BINFMT_MISC` and `X86_MSR` are now off (the whole test suite, `tests/run-all.sh`, passes on that kernel).

## Worth tightening (not done yet)
- Kernel options: `INIT_ON_FREE_DEFAULT_ON`, `ZERO_CALL_USED_REGS`, `DEBUG_FS` off, `IO_URING`
  off (large attack surface), `X86_MSR` off, `BPF_JIT_ALWAYS_ON`, `BINFMT_MISC` off,
  `MODIFY_LDT_SYSCALL` off (Wine/Proton may want it, so check games first), `KALLSYMS_ALL` off.
  Each needs a boot test; they cost a little speed or break some games.
- **Secure Boot**: images are not signed. Plan: ship a shim + MOK enrolment of our own key (the
  kernel already signs modules with a build key), or let users enrol the key in firmware. Until
  then SilverOS needs Secure Boot off (Windows then still boots fine, but BitLocker may ask for its
  recovery key once if the setting changes: have it ready).
