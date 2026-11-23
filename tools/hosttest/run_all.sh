#!/bin/bash
# usage: run_all.sh BINARY [scripts...]
BIN=$(realpath "$1"); shift
cd /home/claude/hosttest/srv && (python3 server.py 2>srv.log &) ; sleep 1
cd /home/claude/hosttest/run
S=${@:-$(ls s*.txt)}
fail=0
for s in $S; do
  rm -f xroot/NSX-*.LOG
  NSX_SCRIPT=$s timeout 120 $BIN > out_$s.log 2>&1; rc=$?
  bad=$(grep -c "ERROR: AddressSanitizer\|runtime error:\|WARNING: ThreadSanitizer\|LeakSanitizer" out_$s.log)
  extra=""; ls xroot/NSX-*.LOG >/dev/null 2>&1 && extra="$(ls xroot/NSX-*.LOG | xargs -n1 basename | tr '\n' ' ')"
  printf "%-8s rc=%-3s san=%-3s %s\n" $s $rc $bad "$extra"
  [ $rc -ne 0 -o $bad -ne 0 ] && fail=1
done
pkill -f "python3 server.py"
exit $fail
