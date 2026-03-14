#!/usr/bin/env bash
set -xeuo pipefail
#
# Universelles Android-Build-Script für:
# - GitHub Actions
# - CircleCI
# - Travis CI
# - AppVeyor (MSYS2/MinGW)
#

echo "=== Working directory ==="
pwd
ls -la

git submodule update --init opencpn-libs

# CI-Erkennung (nur informativ, keine Logikänderung)
CI_SYSTEM="unknown"
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
    CI_SYSTEM="github"
elif [[ "${CIRCLECI:-}" == "true" ]]; then
    CI_SYSTEM="circleci"
elif [[ "${TRAVIS:-}" == "true" ]]; then
    CI_SYSTEM="travis"
elif [[ "${APPVEYOR:-}" == "True" ]]; then
    CI_SYSTEM="appveyor"
fi
echo "Detected CI system: $CI_SYSTEM"

# master.zip Download / Cache (wie vorher, nur robuster)
CIRCLECI_LOCAL_LOWER="${CIRCLECI_LOCAL:-false}"
CIRCLECI_LOCAL_LOWER="${CIRCLECI_LOCAL_LOWER,,}"
MASTER_LOC=$(pwd)

if [[ "$CIRCLECI_LOCAL_LOWER" == "true" && -d ~/circleci-cache ]]; then
    if [[ -f ~/circleci-cache/apt-proxy ]]; then
        sudo tee -a /etc/apt/apt.conf.d/00aptproxy < ~/circleci-cache/apt-proxy
    fi
    if [[ ! -f ~/circleci-cache/master.zip ]]; then
        sudo wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip \
            -O ~/circleci-cache/master.zip
    fi
    MASTER_LOC=~/circleci-cache
else
    wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip
fi

echo "unzipping $MASTER_LOC/master.zip"
unzip -qq -o "$MASTER_LOC/master.zip"

# Dependencies (Linux-Fall wie bisher)
sudo apt-get -q update
sudo apt-get -y install git cmake gettext unzip python3-pip

# Extra build libs
ME=$(echo "${0##*/}" | sed 's/\.sh//g')
for EXTRA_LIBS in ./ci/extras/extra_libs.txt ./ci/extras/${ME}_extra_libs.txt; do
    if [[ -f "$EXTRA_LIBS" ]]; then
        while read -r line; do
            [[ -z "$line" ]] && continue
            sudo apt-get -y install "$line"
        done < "$EXTRA_LIBS"
    fi
done

echo "After deps install:"
pwd
ls -la

mkdir -p build
cd build

rm -f CMakeCache.txt

# Neuere cmake via pip (wie in Script 1)
python3 -m pip install --user --force-reinstall -q pip setuptools
sudo apt-get -y remove python3-six python3-colorama python3-urllib3 || true
export LC_ALL=C.UTF-8 LANG=C.UTF-8
python3 -m pip install --user -q cmake -vv
export PATH="$HOME/.local/bin:$PATH"

# NDK-Pfad (kombiniert aus Script 1 + 2, ohne Verhalten zu brechen)
if ls -d /opt/android/sdk/ndk/* >/dev/null 2>&1; then
    last_ndk=$(ls -d /opt/android/sdk/ndk/* | tail -1)
elif ls -d /home/circleci/android-sdk/ndk/* >/dev/null 2>&1; then
    last_ndk=$(ls -d /home/circleci/android-sdk/ndk/* | tail -1)
else
    echo "ERROR: Could not find Android NDK directory"
    exit 1
fi

test -d /opt/android || sudo mkdir -p /opt/android
sudo ln -sf "$last_ndk" /opt/android/ndk
echo "Using NDK: $last_ndk"

# 🔁 ORIGINAL-BUILD_TYPE-LOGIK AUS SCRIPT 1 (unverändert wiederhergestellt)
if [[ -z "${BUILD_TYPE:-}" ]]; then
    tag=$(git tag --contains HEAD || true)
    current_branch=$(git branch --show-current || true)
    if [[ -n "$tag" ]] || [[ "$current_branch" == "master" ]]; then
        BUILD_TYPE=Release
    else
        BUILD_TYPE=Debug
    fi
fi

echo "Using BUILD_TYPE=$BUILD_TYPE"

cmake -DCMAKE_TOOLCHAIN_FILE=cmake/android-aarch64-toolchain.cmake \
  -D_wx_selected_config=androideabi-qt-arm64 \
  -DwxQt_Build=build_android_release_64_static_O3 \
  -DQt_Build=build_arm64/qtbase \
  -DOCPN_Android_Common=OCPNAndroidCommon-master \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_CXX_FLAGS="-Wno-inconsistent-missing-override -Wno-unused-parameter -Wno-deprecated-declarations" \
  ..

make VERBOSE=1
make package
