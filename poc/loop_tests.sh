#!/bin/sh
# Run each flaky-prone Go test N times with async preemption on/off; keep the first failure.
N=${1:-10}
for t in t_panic t_sig t_fmt t_rt t_fs t_exec t_net; do
  for mode in default; do
    fails=0; i=1
    rm -f /tmp/lt.first.$t.$mode
    while [ $i -le $N ]; do
      if [ $mode = nopreempt ]; then GODEBUG=asyncpreemptoff=1; export GODEBUG; else unset GODEBUG; fi
      timeout -s KILL 20 /root/gotests/$t.bin >/tmp/lt.out 2>&1
      rc=$?
      if [ $rc -ne 0 ]; then
        fails=$((fails+1))
        [ -f /tmp/lt.first.$t.$mode ] || cp /tmp/lt.out /tmp/lt.first.$t.$mode
      fi
      i=$((i+1))
    done
    echo "LOOP $t $mode: fails=$fails/$N"
    if [ -f /tmp/lt.first.$t.$mode ]; then
      echo "--- first failure of $t $mode:"; head -45 /tmp/lt.first.$t.$mode; echo "--- end"
    fi
  done
done
echo LOOPS_DONE
