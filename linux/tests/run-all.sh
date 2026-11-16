#!/usr/bin/env bash
# Run every automated check and print a one-screen summary.
#   tests/run-all.sh            everything (about 45 minutes)
#   tests/run-all.sh quick      the demo checks and the small VM tests only (about 15 minutes)
# Needs a built ISO (./build.sh) and QEMU + OVMF. Logs go to work/test/run-all/.
set -u
cd "$(dirname "$0")/.."
source config.sh
iso="$OUT/silveros-0.1.0-x86_64.iso"
[[ -f "$iso" ]] || { echo "no ISO at $iso: run ./build.sh first"; exit 2; }
t="$WORK/test/run-all"; rm -rf "$t"; mkdir -p "$t"
quick=0; [[ "${1:-}" == quick ]] && quick=1
declare -a names results times
run() {   # run <name> <command...>
  local n=$1; shift; local s=$(date +%s)
  echo ">> $n ..."
  if "$@" > "$t/$n.log" 2>&1; then results+=(PASS); else results+=(FAIL); fi
  names+=("$n"); times+=($(( $(date +%s) - s )))
  pkill -f qemu-system 2>/dev/null; sleep 1
}
vmargs() { echo "$iso $t/disk.img $t/vars.fd $t/$1-out"; }
bash tests/make-fake-windows-disk.sh "$t/disk.img" "$t/vars.fd" > /dev/null 2>&1
run demo-checks bash -c 'bash scripts/render-demo.sh 2>&1 | tee /dev/stderr | grep -q "checks passed" && ! grep -q "CHECK FAILED"'
run power     python3 tests/power-test.py $(vmargs power)
run keyboard  python3 tests/keyboard-test.py $(vmargs keyboard)
run display   python3 tests/display-test.py $(vmargs display)
run idle      python3 tests/idle-test.py $(vmargs idle)
run mouse     python3 tests/mouse-test.py $(vmargs mouse)
run firewall  python3 tests/firewall-test.py $(vmargs firewall)
if [[ $quick = 0 ]]; then
  bash tests/make-fake-windows-disk.sh "$t/disk.img" "$t/vars.fd" > /dev/null 2>&1
  run dual-boot python3 tests/dualboot-test.py "$iso" "$t/disk.img" "$t/vars.fd" "$t/dual-out"
fi
echo; echo "================ SilverOS test summary ================"
fail=0
for i in "${!names[@]}"; do
  printf "  %-14s %-5s %4ss   %s\n" "${names[$i]}" "${results[$i]}" "${times[$i]}" "$t/${names[$i]}.log"
  [[ ${results[$i]} = FAIL ]] && fail=1
done
echo "======================================================="
[[ $fail = 0 ]] && echo "ALL PASSED" || echo "SOMETHING FAILED (read the logs above)"
exit $fail
