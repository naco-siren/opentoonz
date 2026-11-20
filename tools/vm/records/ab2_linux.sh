#!/bin/bash
# Runs inside the nt-asan VM: builds ab_vector.cpp and ab_styles.cpp against
# the libraries of ~/opentoonz/toonz/build-asan as they are now and writes
# their pictures to /var/tmp/ab/<tag>/{vector,styles}; with "golden" as
# second argument also renders the golden scenes into /var/tmp/ab/<tag>/golden.
# (/var/tmp survives a restart of the VM, /tmp does not.)
set -uo pipefail
tag=$1
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
src=~/opentoonz/toonz/sources
b=~/opentoonz/toonz/build-asan
out=/var/tmp/ab/$tag
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
libdir=$(dirname $(find $b -name "libtnzcore.so" | head -1))
cxx=$(grep -m1 "CMAKE_CXX_COMPILER:" $b/CMakeCache.txt | cut -d= -f2)
gtest=$(dirname $(dirname $(find $b/_deps /usr/src -name gtest-all.cc 2>/dev/null | head -1)))
rm -rf $out; mkdir -p $out/vector $out/styles /var/tmp/ab/bin
common="-std=gnu++17 -O1 -fsanitize=address,undefined -fno-sanitize=vptr -DLINUX -Dx64 -DTNZ_LITTLE_ENDIAN=1 -fPIC $(pkg-config --cflags Qt5Gui Qt5Core Qt5OpenGL) -I $src/include"
$cxx $common $here/ab_vector.cpp -L$libdir -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL -Wl,-rpath,$libdir -o /var/tmp/ab/bin/ab_vector_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
$cxx $common -DNEXTTOONZ_FIXTURE_DIR=\"$src/tests/fixtures/reference_project\" -DNEXTTOONZ_STUFF_DIR=\"$HOME/opentoonz/stuff\" \
    -I $src/tests/toonzlib -I $gtest/include -I $gtest $here/ab_styles.cpp $gtest/src/gtest-all.cc \
    -L$libdir -ltoonzlib -limage -ltnzstdfx -lcolorfx -ltnzext -ltnzbase -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL -lpthread \
    -Wl,-rpath,$libdir -o /var/tmp/ab/bin/ab_styles_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
for p in vector styles; do
  QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /var/tmp/ab/bin/ab_${p}_$tag $out/$p 2>&1 | grep -E "pixels drawn|nothing drawn|ERROR|runtime error" | tail -4
  echo "$p: $(ls $out/$p | wc -l) pictures"
done
if [ "${2:-}" = golden ]; then
  cd $b
  QT_QPA_PLATFORM=offscreen ctest -L golden -j 7 --timeout 900 > $out/golden.log 2>&1
  echo "golden ctest exit $?"; grep -E "tests passed|Failed" $out/golden.log | head -6
  mkdir -p $out/golden
  for d in golden/*/; do n=$(basename $d); mkdir -p $out/golden/$n; cp -r $d/out $out/golden/$n/ 2>/dev/null; done
fi
