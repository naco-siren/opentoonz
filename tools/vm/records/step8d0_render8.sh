#!/bin/bash
# Runs inside the nt-asan VM after phase2_step1.sh (doc/nexttoonz_core_noqt.md,
# 8d-0): the reference scenes with -nthreads 8, by the sanitizer build of 8c
# (copied to ~/ab8d0/before before syncing) and of 8d-0, compared byte for
# byte. ASan and UBSan stop at the first report.
set -uo pipefail
AB=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/ab/phase2/ab_tcomposer.py
export QT_QPA_PLATFORM=offscreen
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
rm -rf /var/tmp/ab8d0; mkdir -p /var/tmp/ab8d0
python3 $AB render ~/ab8d0/before/bin/tcomposer /var/tmp/ab8d0/before 8 > /var/tmp/ab8d0/before.log 2>&1
python3 $AB render ~/opentoonz/toonz/build-asan/bin/tcomposer /var/tmp/ab8d0/after 8 > /var/tmp/ab8d0/after.log 2>&1
grep -h "failed:" /var/tmp/ab8d0/before.log /var/tmp/ab8d0/after.log
python3 $AB compare /var/tmp/ab8d0/before /var/tmp/ab8d0/after
