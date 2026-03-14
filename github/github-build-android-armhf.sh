#!/usr/bin/env bash
set -xeo pipefail

echo "Working dir:"
pwd
ls -la

git submodule update --init opencpn-libs

##############################################
# 1. CI-Erkennung
##############################################
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

##############################################
# 2. OCPNAndroidCommon herunterladen
##############################################
MASTER_LOC=$(pwd)

if [[ "$CI_SYSTEM" == "circleci" && "${CIRCLECI_LOCAL,,}" == "true" && -d ~/circleci-cache ]]; then
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

##############################################
# 3. Dependencies installieren
##############################################
case "$CI_SYSTEM" in
    github|circleci|travis)
        sudo apt-get update
        sudo apt-get install -y git cmake gettext unzip python3-pip
        ;;
    appveyor)
        pacman -Sy --noconfirm git cmake unzip python-pip
        ;;
esac

# Extra libs
ME=$(echo "${0##*/}" | sed 's/\.sh//g')
for EXTRA in ./ci/extras/extra_libs.txt ./ci/extras/${ME}_extra_libs.txt; do
    if [[ -f "$EXTRA" ]]; then
        while read -r line; do
            [[ -z "$line" ]] && continue
            case "$CI_SYSTEM" in
                github|circleci|travis)
                    sudo apt-get install -y "$line"
                    ;;
                appveyor)
                    pacman -Sy --noconfirm "$line"
                    ;;
            esac
        done < "$EXTRA"
    fi
done

##############################################
# 4. Python/CMake Workarounds
##############################################
if [[ "$CI_SYSTEM" == "circleci" ]]; then
    python3 -m pip install --user --force-reinstall -q pip setuptools
    sudo apt-get remove -y python3-six python3-colorama python3-urllib3 || true
else
    python3 -m pip install --user --upgrade pip setuptools cmake
fi

export PATH="$HOME/.local/bin:$PATH"
export LC_ALL=C.UTF-8 LANG=C.UTF-8

##############################################
# 5. NDK-Pfad setzen (universell, korrekt)
##############################################
NDK_CANDIDATES=(
    "/opt/android-ndk"                     # GitHub Actions (manuell installiert)
    "/opt/android/sdk/ndk"                 # GitHub Actions Container
    "/home/circleci/android-sdk/ndk"       # CircleCI
    "/c/Android/ndk"                       # AppVeyor Windows
    "/mingw64/android-ndk"                 # MSYS2
)

last_ndk=""

for base in "${NDK_CANDIDATES[@]}"; do
    # Fall 1: Basisordner ist direkt ein NDK (GitHub Actions)
    if [[ -d "$base" && -f "$base/source.properties" ]]; then
        last_ndk="$base"
        break
    fi

    # Fall 2: Basisordner enthält Versionen (CircleCI, Windows)
    if [[ -d "$base" ]]; then
        for d in "$base"/*; do
            if [[ -d "$d" && -f "$d/source.properties" ]]; then
                last_ndk="$d"
            fi
        done
        [[ -n "$last_ndk" ]] && break
    fi
done

if [[ -z "$last_ndk" ]]; then
    echo "ERROR: Could not find Android NDK directory"
    exit 1
fi

sudo mkdir -p /opt/android
sudo ln -sfn "$last_ndk" /opt/android/ndk

export ANDROID_NDK_HOME="/opt/android/ndk"
export ANDROID_NDK="/opt/android/ndk"
export ANDROID_NDK_ROOT="/opt/android/ndk"

echo "Using NDK: $last_ndk"


##############################################
# 6. Build vorbereiten
##############################################
mkdir -p build
cd build
rm -f CMakeCache.txt

# Build-Type bestimmen (unverändert)
if [[ -z "${BUILD_TYPE:-}" ]]; then
    tag=$(git tag --contains HEAD || true)
    branch=$(git branch --show-current || true)
    if [[ -n "$tag" || "$branch" == "master" ]]; then
        BUILD_TYPE=Release
    else
        BUILD_TYPE=Debug
    fi
fi

echo "Using BUILD_TYPE=$BUILD_TYPE"

##############################################
# 7. CMake + Build
##############################################
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/android-armhf-toolchain.cmake \
  -D_wx_selected_config=androideabi-qt-armhf \
  -DwxQt_Build=build_android_release_19_static_O3 \
  -DQt_Build=build_arm32_19_O3/qtbase \
  -DOCPN_Android_Common=OCPNAndroidCommon-master \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_CXX_FLAGS="-Wno-inconsistent-missing-override -Wno-unused-parameter -Wno-deprecated-declarations" \
  ..

make VERBOSE=1
make package
