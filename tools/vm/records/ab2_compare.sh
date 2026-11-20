#!/bin/bash
# Runs inside the VM: compares /var/tmp/ab/<a> with /var/tmp/ab/<b>.
a=/var/tmp/ab/$1; b=/var/tmp/ab/$2
for p in vector styles golden regions strokes; do
  [ -d $a/$p ] && [ -d $b/$p ] || continue
  s=0; d=0
  for f in $(cd $a/$p && find . -type f \( -name "*.raw" -o -name "*.png" \)); do
    if cmp -s $a/$p/$f $b/$p/$f; then s=$((s+1)); else d=$((d+1)); echo "DIFFERS $p/$f ($(cmp -l $a/$p/$f $b/$p/$f 2>/dev/null | wc -l) bytes)"; fi
  done
  echo "$p: $s identical, $d different"
done
