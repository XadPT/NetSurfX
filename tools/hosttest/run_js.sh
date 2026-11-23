#!/bin/bash
# JavaScript checks (QuickJS on): run_js.sh BINARY
BIN=$(realpath "$1")
cd /home/claude/hosttest/srv && (python3 server.py 2>srv.log &) ; sleep 1
cd /home/claude/hosttest/run
fail=0
for t in "js1 zzjs2.txt" "jsloop zzjs2.txt" "jsmem zzjs2.txt" "js2 zzjs5.txt"; do
  set -- $t
  rm -f xjs/NETSURF.* xjs/NSX-*
  sed -i "s|^homepage_url:.*|homepage_url:http://127.0.0.1:8080/static/$1.html|" xjs/data/Choices
  NSX_ROOT=xjs NSX_SCRIPT=$2 timeout 120 $BIN > outjs_$1.log 2>&1; rc=$?
  bad=$(grep -c "ERROR: AddressSanitizer\|runtime error:\|WARNING: ThreadSanitizer\|LeakSanitizer" outjs_$1.log)
  ok=$(grep -c "JS context ready" xjs/NETSURF.LOG)
  printf "%-7s rc=%-3s san=%-3s jsctx=%s\n" $1 $rc $bad $ok
  [ $rc -ne 0 -o $bad -ne 0 -o $ok -eq 0 ] && { fail=1; cp outjs_$1.log fail_js_$1.log; }
done
pkill -f "python3 server.py"
exit $fail
