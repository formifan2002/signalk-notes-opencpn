#!/usr/bin/env bash
set -e

echo "=== Building OpenCPN Plugin for Ubuntu 24.04 (Noble) x86_64 ==="

docker run --rm \
  -v $(pwd):/workspace \
  -w /workspace \
  ubuntu:24.04 \
  bash -c "
    set -e

    apt-get update &&
    apt-get install -y \
      cmake g++ make git \
      libwxgtk3.2-dev wx-common \
      libglu1-mesa-dev freeglut3-dev mesa-common-dev \
      libcurl4-openssl-dev \
      libtinyxml2-dev \
      libarchive-dev \
      zlib1g-dev \
      pkg-config

    echo '=== Cloning OpenCPN Plugin SDK ==='
    git clone --depth=1 https://github.com/OpenCPN/OpenCPN.git /tmp/opencpn

    mkdir -p build &&
    cd build &&
    cmake \
      -DOCPN_PLUGIN=ON \
      -DwxWidgets_CONFIG_EXECUTABLE=/usr/bin/wx-config \
      -DOCPN_SDK_PATH=/tmp/opencpn \
      -DCMAKE_BUILD_TYPE=Release \
      .. &&
    make -j\$(nproc)
  "

echo "=== Build finished ==="
