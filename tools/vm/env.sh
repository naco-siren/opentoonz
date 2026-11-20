# shellcheck shell=bash
# Sourced by the scripts in tools/vm that run inside the VM. Every setting has
# a default and can be overridden from the environment (tools/vm/run.sh
# passes NT_* variables through):
#   NT_CLONE    the VM's own clone, on the VM's disk       (default ~/opentoonz)
#   NT_BUILD    its build directory, the asan preset's one  (default $NT_CLONE/toonz/build-asan)
#   NT_TARGETS  what build.sh and sync.sh build             (the Sanitizers CI job's targets)
#   NT_SKIA     vcpkg's Skia, as the job builds it           (default ~/skia-vcpkg; build.sh
#               makes it with ci-scripts/skia/vcpkg_skia.sh when missing)
NT_CLONE=${NT_CLONE:-$HOME/opentoonz}
NT_CLONE=${NT_CLONE/#\~/$HOME}        # a ~ passed from the host is the VM's home
NT_BUILD=${NT_BUILD:-$NT_CLONE/toonz/build-asan}
NT_BUILD=${NT_BUILD/#\~/$HOME}
NT_TARGETS=${NT_TARGETS:-nexttoonz_core_tests nexttoonz_toonzlib_tests nexttoonz_canvas_tests nexttoonz_canvas_skia_tests tcomposer lzocompress lzodecompress}
NT_SKIA=${NT_SKIA:-$HOME/skia-vcpkg}
NT_SKIA=${NT_SKIA/#\~/$HOME}
NT_LOGS=${NT_LOGS:-/tmp/nt-vm}
mkdir -p "$NT_LOGS"
# The Sanitizers job's environment for building (test discovery runs the binaries).
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}

# Brings the clone to origin/<branch> (the branch as committed on the host).
nt_checkout() {
    cd "$NT_CLONE" || exit 1
    git fetch -q origin
    git checkout -q "$1" 2>/dev/null || git checkout -q -b "$1" "origin/$1"
    git reset -q --hard "origin/$1"
    git log --oneline -1
}
