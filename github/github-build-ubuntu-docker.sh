#!/usr/bin/env bash

#
# Build for Raspbian and debian in a docker container
#
# D.B.: start - always move to repo root, independent of CI system cd "$(dirname "$0")/.."
cd "$(dirname "$0")/.."
ls -la
# D.B.: ends- always move to repo root, independent of CI system cd "$(dirname "$0")/.."

git submodule update --init opencpn-libs

# D.B.: start - Detect OCPN API version from CMakeLists.txt and apply <cstdint> patch ONLY for "API < 20 & Trixie ARM64 builds"
API_MINOR=$(grep -Eo 'set\(OCPN_API_VERSION_MINOR "[0-9]+"\)' CMakeLists.txt \
            | grep -Eo '[0-9]+')
# Prüfen, ob API_MINOR überhaupt gefunden wurde
if [ -z "$API_MINOR" ]; then
    echo "ERROR: Could not read OCPN_API_VERSION_MINOR from CMakeLists.txt."
    exit 1
fi
echo "API-Version: $API_MINOR"
echo "OCPN_TARGET='$OCPN_TARGET'"
echo "GITHUB_ACTIONS='$GITHUB_ACTIONS'"
echo "USE_UNSTABLE_REPO='$USE_UNSTABLE_REPO'"

if [ "$API_MINOR" -lt 20 ] && [[ "$OCPN_TARGET" == *"trixie-arm64"* ]]; then
    echo "Applying <cstdint> patch for API < 20 on Trixie ARM64"
    sed -i 's/#include <unordered_map>/#include <unordered_map>\n#include <cstdint>/' \
        ci-source/opencpn-libs/api-18/ocpn_plugin.h
fi
# D.B.: end - Detect OCPN API version from CMakeLists.txt and apply <cstdint> patch ONLY for "API < 20 & Trixie ARM64 builds"

# bailout on errors and echo commands.
set -x
sudo apt-get -y --allow-unauthenticated update

DOCKER_SOCK="unix:///var/run/docker.sock"

echo "DOCKER_OPTS=\"-H tcp://127.0.0.1:2375 -H $DOCKER_SOCK -s devicemapper\"" | sudo tee /etc/default/docker > /dev/null
sudo service docker restart
sleep 5;

if [ "$BUILD_ENV" = "raspbian" ]; then
    docker run --rm --privileged multiarch/qemu-user-static:register --reset
else
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
fi

# TTY nur deaktivieren, wenn wir in GitHub Actions laufen
TTY_FLAG=""
if [ "$GITHUB_ACTIONS" != "true" ]; then
    TTY_FLAG="-ti"
fi

# Dynamisch ENV-Variablen sammeln
ENV_ARGS=()

add_env() {
    local name="$1"
    local value="$2"
    if [ -n "$value" ]; then
        ENV_ARGS+=("-e" "$name=$value")
    fi
}
add_env "container" "docker"
add_env "CIRCLECI" "$CIRCLECI"
add_env "CIRCLE_BRANCH" "$CIRCLE_BRANCH"
add_env "CIRCLE_TAG" "$CIRCLE_TAG"
add_env "CIRCLE_PROJECT_USERNAME" "$CIRCLE_PROJECT_USERNAME"
add_env "CIRCLE_PROJECT_REPONAME" "$CIRCLE_PROJECT_REPONAME"
add_env "GIT_REPOSITORY_SERVER" "$GIT_REPOSITORY_SERVER"
add_env "OCPN_TARGET" "$OCPN_TARGET"
add_env "BUILD_GTK3" "$BUILD_GTK3"
add_env "WX_VER" "$WX_VER"
add_env "BUILD_ENV" "$BUILD_ENV"
add_env "TZ" "$TZ"
add_env "DEBIAN_FRONTEND" "$DEBIAN_FRONTEND"
add_env "USE_UNSTABLE_REPO" "$USE_UNSTABLE_REPO"

# Architektur aus OCPN_TARGET ableiten
case "$OCPN_TARGET" in
    *arm64*)
        PLATFORM_FLAG="--platform=linux/arm64"
        ;;
    *armhf*)
        PLATFORM_FLAG="--platform=linux/arm/v7"
        ;;
    *)
        PLATFORM_FLAG="--platform=linux/amd64"
        ;;
esac

echo "Using platform: $PLATFORM_FLAG"

# Container starten
DOCKER_CONTAINER_ID=$(docker run $PLATFORM_FLAG --privileged -d $TTY_FLAG \
    "${ENV_ARGS[@]}" \
    -v "$(pwd)":/ci-source:rw \
    -v ~/source_top:/source_top \
    "$DOCKER_IMAGE" \
    tail -f /dev/null)

if [ -z "$DOCKER_CONTAINER_ID" ]; then
    echo "ERROR: Failed to get Docker container ID"
    exit 1
fi
echo "Docker Container ID: $DOCKER_CONTAINER_ID"
# >>>>> ARM64 Stabilitätsfix: Python Bytecode deaktivieren + Swap aktivieren <<<<<
if [[ "$OCPN_TARGET" == *"arm64"* ]]; then
    echo "Applying ARM64 stability fixes (no bytecode + swap)"

    # Python Bytecode ausschalten (verhindert QEMU-BrokenPipe bei python3)
    docker exec "$DOCKER_CONTAINER_ID" bash -c "echo 'PYTHONDONTWRITEBYTECODE=1' >> /etc/environment"

    # Swap aktivieren (verhindert QEMU-Speicherfehler)
    docker exec "$DOCKER_CONTAINER_ID" bash -c "
        fallocate -l 2G /swapfile
        chmod 600 /swapfile
        mkswap /swapfile
        swapon /swapfile
    "
fi
# >>>>> ENDE ARM64 Stabilitätsfix <<<<<

# >>>>> NEU: unstable-Repo direkt nach Containerstart aktivieren <<<<<
if [ "$GITHUB_ACTIONS" = "true" ] && [ "$USE_UNSTABLE_REPO" = "ON" ]; then
    echo "Enabling unstable repo inside container (EARLY)"
    docker exec "$DOCKER_CONTAINER_ID" bash -c "
        echo 'deb http://deb.debian.org/debian unstable main' >> /etc/apt/sources.list
        apt-get update
        apt-get -y --fix-broken --fix-missing install
    "
fi
# >>>>> ENDE NEU <<<<<

echo "Target build: $OCPN_TARGET"
rm -f build.sh

delimstrnum=1

if [ "$BUILD_ENV" = "raspbian" ]; then

    if [ "$OCPN_TARGET" = "buster-armhf" ]; then
        cat >> build.sh << EOF$delimstrnum
        # cmake 3.16 has a bug that stops the build to use an older version
        install_packages cmake=3.13.4-1 cmake-data=3.13.4-1
EOF$delimstrnum
        ((delimstrnum++))
    else
        cat >> build.sh << EOF$delimstrnum
        install_packages cmake cmake-data
EOF$delimstrnum
        ((delimstrnum++))
    fi

    if [ "$OCPN_TARGET" = "bullseye-armhf" ]; then
        cat >> build.sh << EOF$delimstrnum
        curl http://mirrordirector.raspbian.org/raspbian.public.key  | apt-key add -
        curl http://archive.raspbian.org/raspbian.public.key  | apt-key add -
        sudo apt -q --allow-unauthenticated update
        sudo apt --allow-unauthenticated install equivs wget git lsb-release
        sudo mk-build-deps -ir ci-source/ci/control
        sudo apt-get --allow-unauthenticated install -f
EOF$delimstrnum
        ((delimstrnum++))
    else
        cat >> build.sh << EOF$delimstrnum
        install_packages git build-essential equivs gettext wx-common libgtk2.0-dev libwxbase3.0-dev libwxgtk3.0-dev libbz2-dev libcurl4-openssl-dev libexpat1-dev libcairo2-dev libarchive-dev liblzma-dev libexif-dev lsb-release
EOF$delimstrnum
        ((delimstrnum++))
    fi

else

    if [ "$OCPN_TARGET" = "bullseye-armhf" ] ||
       [ "$OCPN_TARGET" = "bullseye-arm64" ] ||
       [ "$OCPN_TARGET" = "bookworm-armhf" ] ||
       [ "$OCPN_TARGET" = "bookworm-arm64" ] ||
       [ "$OCPN_TARGET" = "bookworm" ] ||
       [ "$OCPN_TARGET" = "trixie-armhf" ] ||
       [ "$OCPN_TARGET" = "trixie-arm64" ] ||
       [ "$OCPN_TARGET" = "trixie" ] ||
       [ "$OCPN_TARGET" = "buster-armhf" ]; then

        cat >> build.sh << EOF$delimstrnum
        echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections
        apt-get -qq --allow-unauthenticated update && DEBIAN_FRONTEND='noninteractive' TZ='America/New_York' apt-get -y --no-install-recommends --allow-change-held-packages install tzdata
        apt-get -y --fix-missing install --allow-change-held-packages --allow-unauthenticated  \
        equivs wget git build-essential gettext wx-common libgtk2.0-dev libbz2-dev libcurl4-openssl-dev libexpat1-dev libcairo2-dev libarchive-dev liblzma-dev libexif-dev lsb-release openssl libssl-dev
EOF$delimstrnum
        ((delimstrnum++))

        if [ "$OCPN_TARGET" = "bullseye-armhf" ] ||
           [ "$OCPN_TARGET" = "bullseye-arm64" ] ||
           [ "$OCPN_TARGET" = "bookworm-armhf" ] ||
           [ "$OCPN_TARGET" = "bookworm-arm64" ] ||
           [ "$OCPN_TARGET" = "bookworm" ] ||
           [ "$OCPN_TARGET" = "buster-armhf" ]; then

            cat >> build.sh << EOF$delimstrnum
                apt-get -y --fix-missing --allow-change-held-packages --allow-unauthenticated install software-properties-common
EOF$delimstrnum
            ((delimstrnum++))
        fi

        if [ "$OCPN_TARGET" = "buster-armhf" ] ||
           [ "$OCPN_TARGET" = "bullseye-arm64" ]; then

            echo "BUILD_GTK3: $BUILD_GTK3"
            if [ ! -n "$BUILD_GTK3" ] || [ "$BUILD_GTK3" = "false" ]; then
                echo "Building for GTK2"
                cat >> build.sh << EOF$delimstrnum
                apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install libwxgtk3.0-dev
EOF$delimstrnum
                ((delimstrnum++))
            else
                echo "Building for GTK3"
                cat >> build.sh << EOF$delimstrnum
                apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install libwxgtk3.0-gtk3-dev
EOF$delimstrnum
                ((delimstrnum++))
            fi
        fi

        echo "WX_VER: $WX_VER"
        if [ ! -n "$WX_VER" ] || [ "$WX_VER" = "30" ]; then
            echo "Building for WX30"
            cat >> build.sh << EOF$delimstrnum
            apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install libwxbase3.0-dev
EOF$delimstrnum
            ((delimstrnum++))

        elif [ "$WX_VER" = "32" ]; then
            echo "Building for WX32"

            if [ "$OCPN_TARGET" = "bullseye-armhf" ] || [ "$OCPN_TARGET" = "bullseye-arm64" ]; then
                cat >> build.sh << EOF$delimstrnum
                echo "deb [trusted=yes] https://ppa.launchpadcontent.net/opencpn/opencpn/ubuntu jammy main" | tee -a /etc/apt/sources.list
                echo "deb-src [trusted=yes] https://ppa.launchpadcontent.net/opencpn/opencpn/ubuntu jammy main" | tee -a /etc/apt/sources.list
                apt-get -y --allow-unauthenticated update
EOF$delimstrnum
                ((delimstrnum++))
            fi

            cat >> build.sh << EOF$delimstrnum
            apt-get -y --fix-missing --allow-change-held-packages --allow-unauthenticated install libwxgtk3.2-dev
EOF$delimstrnum
            ((delimstrnum++))
        fi

        if [ "$OCPN_TARGET" = "focal-armhf" ]; then
            cat >> build.sh << EOF$delimstrnum
            CMAKE_VERSION=3.20.5-0kitware1ubuntu20.04.1
            wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc --no-check-certificate 2>/dev/null | apt-key add -
            apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
            apt-get --allow-unauthenticated update
            apt --allow-unauthenticated install cmake=\$CMAKE_VERSION cmake-data=\$CMAKE_VERSION
EOF$delimstrnum
            ((delimstrnum++))
        else
            cat >> build.sh << EOF$delimstrnum
            apt install -y --allow-unauthenticated cmake
EOF$delimstrnum
            ((delimstrnum++))
        fi

    else
        cat > build.sh << EOF$delimstrnum
        apt-get -qq --allow-unauthenticated update
        apt-get -y --no-install-recommends --allow-change-held-packages --allow-unauthenticated install \
        git cmake build-essential gettext wx-common libgtk2.0-dev libwxbase3.0-dev libwxgtk3.0-dev libbz2-dev libcurl4-openssl-dev libexpat1-dev libcairo2-dev libarchive-dev liblzma-dev libexif-dev lsb-release
EOF$delimstrnum
        ((delimstrnum++))
    fi
fi

# Install extra build libs
ME=$(echo ${0##*/} | sed 's/\.sh//g')

cat >> build.sh <<EOF$delimstrnum
# Install extra build libs
if [ -f "ci-source/github/extras/extra_libs.txt" ]; then
    echo "Installing extra libs from extra_libs.txt"
    while read line; do
        [ -z "\$line" ] && continue
        apt-get install -y --allow-unauthenticated \$line || true
    done < ci-source/github/extras/extra_libs.txt
fi

if [ -f "ci-source/github/extras/${ME}_extra_libs.txt" ]; then
    echo "Installing script-specific extra libs for ${ME}"
    while read line; do
        [ -z "\$line" ] && continue
        apt-get install -y --allow-unauthenticated \$line || true
    done < ci-source/github/extras/${ME}_extra_libs.txt
fi
EOF$delimstrnum
((delimstrnum++))

echo "Build script --- start ----"
set +x
cat build.sh
set -x
echo "Build script --- end ---"

if type nproc &> /dev/null; then
    # ARM64 unter QEMU → nur 1 Job, sonst ICE
    if [[ "$OCPN_TARGET" == *"arm64"* ]]; then
        BUILD_FLAGS="-j1"
    else
        BUILD_FLAGS="-j$(nproc)"
    fi
else
    BUILD_FLAGS="-j1"
fi
EXTRA_CMAKE_FLAGS=""
if [[ "$OCPN_TARGET" == *"trixie"* ]] && [[ "$OCPN_TARGET" == *"arm64"* ]]; then
    EXTRA_CMAKE_FLAGS="-DCMAKE_CXX_FLAGS_RELEASE='-O2' -DCMAKE_C_FLAGS_RELEASE='-O2'"
fi
VERBOSE_FLAG=${CMAKE_DETAILLED_LOG:-ON}
docker exec $TTY_FLAG \
    "$DOCKER_CONTAINER_ID" /bin/bash -xec "
        bash -xe ci-source/build.sh;
        rm -rf ci-source/build;
        mkdir ci-source/build;
        cd ci-source/build;
        cmake .. -DCMAKE_VERBOSE_MAKEFILE=${VERBOSE_FLAG} ${EXTRA_CMAKE_FLAGS};
        make $BUILD_FLAGS;
        make package;
        chmod -R a+rw ../build;
    "

echo "Stopping"
docker ps -a
docker stop $DOCKER_CONTAINER_ID
docker rm -v $DOCKER_CONTAINER_ID
