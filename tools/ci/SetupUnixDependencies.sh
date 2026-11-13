#!/usr/bin/env bash
set -euo pipefail

# CI prepares dependencies before CMake runs. Project configuration stays offline.
prefix="${1:?Usage: SetupUnixDependencies.sh <install-prefix>}"
source_root="${RUNNER_TEMP:-/tmp}/lux-native-sources"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

case "$(uname -s)" in
    Linux)
        sudo apt-get update
        sudo apt-get install --no-install-recommends -y \
            build-essential ninja-build pkg-config libzip-dev nlohmann-json3-dev \
            libvulkan-dev glslang-tools libx11-dev libxext-dev libxcursor-dev \
            libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev \
            libwayland-dev wayland-protocols libxkbcommon-dev libegl1-mesa-dev \
            libgl1-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev \
            libudev-dev libdbus-1-dev
        ;;
    Darwin)
        brew install ninja pkgconf libzip nlohmann-json
        ;;
    *)
        printf 'Unsupported CI platform: %s\n' "$(uname -s)" >&2
        exit 1
        ;;
esac

fetch_source() {
    local repository="$1"
    local revision="$2"
    local directory="$3"
    mkdir -p "$directory"
    git -C "$directory" init --quiet
    if git -C "$directory" remote get-url origin >/dev/null 2>&1; then
        git -C "$directory" remote set-url origin "$repository"
    else
        git -C "$directory" remote add origin "$repository"
    fi
    git -C "$directory" fetch --depth 1 origin "$revision"
    git -C "$directory" checkout --quiet --detach FETCH_HEAD
}

# These versions are shared by Debug and Release jobs. Unix dependency ABIs are compatible.
if [[ ! -f "$prefix/lib/cmake/SDL3/SDL3Config.cmake" ]]; then
    sdl_source="$source_root/SDL"
    fetch_source https://github.com/libsdl-org/SDL.git \
        f5e5f6588921eed3d7d048ce43d9eb1ff0da0ffc "$sdl_source" # SDL 3.2.30
    cmake -S "$sdl_source" -B "$source_root/sdl-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build "$source_root/sdl-build" --parallel "$jobs"
    cmake --install "$source_root/sdl-build"
fi

# Homebrew's Box2D is 3.x; LuxEngine deliberately requires the incompatible 2.4 API.
if [[ ! -f "$prefix/lib/cmake/box2d/box2dConfig.cmake" ]]; then
    box_source="$source_root/box2d"
    fetch_source https://github.com/erincatto/box2d.git \
        9ebbbcd960ad424e03e5de6e66a40764c16f51bc "$box_source" # Box2D 2.4.1
    cmake -S "$box_source" -B "$source_root/box2d-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBOX2D_BUILD_UNIT_TESTS=OFF -DBOX2D_BUILD_TESTBED=OFF -DBOX2D_BUILD_DOCS=OFF
    cmake --build "$source_root/box2d-build" --parallel "$jobs"
    cmake --install "$source_root/box2d-build"
fi

if [[ ! -f "$prefix/include/stb/stb_image.h" ]]; then
    stb_source="$source_root/stb"
    fetch_source https://github.com/nothings/stb.git \
        9600522de53973336fa033a289858a4354cbb04c "$stb_source"
    mkdir -p "$prefix/include/stb"
    cp "$stb_source/stb_image.h" "$stb_source/stb_image_write.h" "$prefix/include/stb/"
fi
