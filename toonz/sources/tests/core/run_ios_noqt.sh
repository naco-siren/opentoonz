#!/bin/bash
# Builds the part of tnzcore that is off Qt, with the core tests that need no
# more, for iPadOS and, for the simulator, runs them (see ../ios_gtest.sh).
# The file lists are read from the CMake lists the WITH_QT=OFF build uses
# (TNZCORE_NOQT_SOURCES in tnzcore/CMakeLists.txt and
# NEXTTOONZ_CORE_NOQT_TEST_SOURCES in tests/CMakeLists.txt), so this builds
# exactly what that build does.
#
#   run_ios_noqt.sh <simulator|device> <googletest source dir> <work dir>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
sources=$here/../..

# The entries of a set(<name> ...) block of a CMakeLists.txt, one per line.
cmake_list() {
  sed -n "/^set($2\$/,/^)/p" "$1" | sed '1d;$d' | sed 's/^ *//'
}

files=()
while read -r f; do files+=("$sources/tnzcore/$f"); done \
  < <(cmake_list "$sources/tnzcore/CMakeLists.txt" TNZCORE_NOQT_SOURCES)
while read -r f; do files+=("${f/\$\{CMAKE_CURRENT_SOURCE_DIR\}/$sources/tests}"); done \
  < <(cmake_list "$sources/tests/CMakeLists.txt" NEXTTOONZ_CORE_NOQT_TEST_SOURCES)

# tnztypes.h picks the integer types by platform macro; the Apple one fits.
exec "$here/../ios_gtest.sh" "$1" "$2" "$3" nexttoonz_core_noqt_tests -- \
    -DMACOSX -DTNZ_LITTLE_ENDIAN=1 -Dx64 \
    -DTNZCORE_EXPORTS -DTCOLOR_EXPORTS -DTGEOMETRY_EXPORTS -DTVECTORIMAGE_EXPORTS -DTSOUND_EXPORTS \
    -I"$sources/include" "${files[@]}"
