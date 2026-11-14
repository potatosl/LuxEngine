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
            build-essential ninja-build pkg-config libzip-dev zipcmp zipmerge ziptool \
            nlohmann-json3-dev \
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

# SDL shares its ABI across configurations. Jolt installs matching Debug and Release archives.
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

# Offline project configuration consumes this separately installed Jolt package.
if [[ ! -f "$prefix/lib/cmake/Jolt/JoltConfig.cmake" ||
      ! -f "$prefix/lib/cmake/Jolt/JoltConfig-debug.cmake" ||
      ! -f "$prefix/lib/cmake/Jolt/JoltConfig-release.cmake" ]]; then
    jolt_source="$source_root/jolt"
    fetch_source https://github.com/jrouwe/JoltPhysics.git \
        e77f175595e64cb44218cc9d9d56fc365ad0e36a "$jolt_source" # Jolt 5.6.0
    for configuration in Debug Release; do
    cmake -S "$jolt_source/Build" -B "$source_root/jolt-build" -G Ninja \
        -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_DEBUG_POSTFIX=d -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_SAMPLES=OFF \
        -DTARGET_VIEWER=OFF -DTARGET_PERFORMANCE_TEST=OFF \
        -DJPH_USE_DX12=OFF -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF \
        -DINTERPROCEDURAL_OPTIMIZATION=OFF -DOVERRIDE_CXX_FLAGS=OFF \
        -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF
    cmake --build "$source_root/jolt-build" --parallel "$jobs"
    cmake --install "$source_root/jolt-build"
    done
fi

if [[ ! -f "$prefix/include/stb/stb_image.h" ||
      ! -f "$prefix/include/stb/stb_truetype.h" ]]; then
    stb_source="$source_root/stb"
    fetch_source https://github.com/nothings/stb.git \
        9600522de53973336fa033a289858a4354cbb04c "$stb_source"
    mkdir -p "$prefix/include/stb"
    cp "$stb_source"/*.h "$prefix/include/stb/"
fi

# CMake keeps deployed runtime RPATHs relative. CI dependencies live in a separate prefix,
# so make that installation discoverable without changing the package's relocation contract.
if [[ -n "${GITHUB_ENV:-}" ]]; then
    case "$(uname -s)" in
        Linux)
            printf 'LD_LIBRARY_PATH=%s/lib%s\n' "$prefix" \
                "${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" >> "$GITHUB_ENV"
            ;;
        Darwin)
            printf 'DYLD_LIBRARY_PATH=%s/lib%s\n' "$prefix" \
                "${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" >> "$GITHUB_ENV"
            ;;
    esac
fi
