#!/bin/bash
# Runs inside the VM: installs what workflow_sanitizers.yml installs, clones the
# (read-only mounted) repository to the VM's disk, checks out <branch> and
# builds the bundled libtiff in the clone.
# usage: provision.sh <branch>     (NT_SRC: the repository to clone; default:
#        the main checkout of the checkout this script is in, as mounted)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/env.sh"
branch=${1:?usage: provision.sh <branch>}
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
# shellcheck disable=SC2024  # the log is the user's, not root's
sudo apt-get install -y -qq build-essential cmake pkg-config ninja-build ccache libboost-all-dev qtbase5-dev qt5-qmake qtscript5-dev qttools5-dev qttools5-dev-tools qtmultimedia5-dev qtwayland5 libqt5svg5-dev libqt5opengl5-dev libqt5multimedia5-plugins libqt5serialport5-dev libsuperlu-dev liblz4-dev libusb-1.0-0-dev liblzo2-dev libpng-dev libjpeg-dev libglew-dev freeglut3-dev libfreetype6-dev libjson-c-dev libmypaint-dev libopencv-dev libturbojpeg-dev libomp-dev libfuse2 libgtest-dev xvfb mesa-utils libgl1-mesa-dri python3-numpy python3-pil clang libclang-rt-18-dev git > "$NT_LOGS/apt.log" 2>&1
# The mount belongs to the host's user id.
git config --global --add safe.directory '*'
if [ -z "${NT_SRC:-}" ]; then
    NT_SRC=$(dirname "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)")
fi
if [ ! -d "$NT_CLONE/.git" ]; then
    git clone -q "$NT_SRC" "$NT_CLONE"
fi
nt_checkout "$branch"
if [ ! -f thirdparty/tiff-4.0.3/libtiff/.libs/libtiff.a ]; then
    cd thirdparty/tiff-4.0.3
    CFLAGS='-fPIC' CXXFLAGS='-fPIC' ./configure --disable-jbig > "$NT_LOGS/tiff-configure.log" 2>&1
    make -j "$(nproc)" > "$NT_LOGS/tiff-make.log" 2>&1
fi
echo "provisioned $NT_CLONE from $NT_SRC"
