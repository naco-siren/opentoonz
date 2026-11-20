#!/bin/bash
# Runs inside the nt-asan VM: builds ab_vector.cpp against the libraries of
# ~/opentoonz/toonz/build-asan as they are now and writes its pictures to
# /tmp/ab_<tag>; with "golden" as second argument also renders the golden
# scenes and keeps their output in /tmp/golden_<tag>.
set -uo pipefail
tag=$1
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
b=~/opentoonz/toonz/build-asan
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
libdir=$(dirname $(find $b -name "libtnzcore.so" | head -1))
cxx=$(grep -m1 "CMAKE_CXX_COMPILER:" $b/CMakeCache.txt | cut -d= -f2)
mkdir -p /tmp/abbin /tmp/ab_$tag
$cxx -std=gnu++17 -O1 -fsanitize=address,undefined -fno-sanitize=vptr -DLINUX -Dx64 -DTNZ_LITTLE_ENDIAN=1 -fPIC \
    $(pkg-config --cflags Qt5Gui Qt5Core Qt5OpenGL) -I ~/opentoonz/toonz/sources/include \
    $here/ab_vector.cpp -L$libdir -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL \
    -Wl,-rpath,$libdir -o /tmp/abbin/ab_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /tmp/abbin/ab_$tag /tmp/ab_$tag 2>&1 | grep -v "create context\|varName" | tail -3
ls /tmp/ab_$tag | wc -l
if [ "${2:-}" = golden ]; then
  cd $b
  QT_QPA_PLATFORM=offscreen ctest -L golden -j 7 --timeout 900 > /tmp/golden_$tag.log 2>&1
  echo "golden ctest exit $?"; grep -E "tests passed|Failed" /tmp/golden_$tag.log | head -8
  rm -rf /tmp/golden_$tag; mkdir -p /tmp/golden_$tag
  for d in golden/*/; do n=$(basename $d); mkdir -p /tmp/golden_$tag/$n; cp -r $d/out /tmp/golden_$tag/$n/ 2>/dev/null; done
  du -sh /tmp/golden_$tag | cut -f1
fi
