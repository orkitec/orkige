# Local reproduction of the CI Linux jobs (ubuntu 24.04), native-arch
# flavor (arm64 on Apple-Silicon hosts). Mirrors .github/workflows/ci.yml:
# same apt set, clang as the compiler, pinned vcpkg commit. The repo is
# bind-mounted at runtime; the vcpkg binary cache, ccache and the build
# trees live in named volumes (see run_container.sh). All Linux presets
# work inside, including linux-debug-sanitize - the ASan/UBSan gate on
# libstdc++, which sees memory bugs libc++ (macOS) masks.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# clang-tools provides clang-scan-deps, which vcpkg's CMake requires for
# C++20 module scanning during port builds (absent = ports fail at ninja)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential clang clang-tools libclang-rt-18-dev ninja-build ccache python3 pkg-config \
    cmake git ca-certificates gdb \
    autoconf autoconf-archive automake libtool curl zip unzip tar \
    libx11-dev libxft-dev libxext-dev libxrandr-dev libxi-dev \
    libxcursor-dev libxfixes-dev libxrender-dev libxss-dev \
    libxkbcommon-dev libxkbcommon-x11-dev libxt-dev libxaw7-dev \
    libx11-xcb-dev libxcb-randr0-dev libxtst-dev libxinerama-dev \
    libxcb1-dev libxcb-keysyms1-dev libxcb-ewmh-dev libxcb-icccm4-dev \
    libwayland-dev wayland-protocols libdecor-0-dev \
    libegl1-mesa-dev libgl1-mesa-dev libglu1-mesa-dev \
    libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libltdl-dev \
    libibus-1.0-dev \
    xvfb mesa-utils libgl1-mesa-dri \
    mesa-vulkan-drivers vulkan-tools \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf $(command -v clang-scan-deps-18 || command -v clang-scan-deps) \
       /usr/local/bin/clang-scan-deps

ENV CC=clang CXX=clang++ \
    CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=4G \
    CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
    VCPKG_DEFAULT_BINARY_CACHE=/vcpkg-cache

# Pinned vcpkg, same commit as CI (env VCPKG_COMMIT in ci.yml). The presets
# resolve VCPKG_ROOT to $HOME/Development/vcpkg.
ARG VCPKG_COMMIT=42e4e33e1505c9f47b58c21e0f557c1571b751ee
RUN git clone https://github.com/microsoft/vcpkg /root/Development/vcpkg \
    && git -C /root/Development/vcpkg checkout ${VCPKG_COMMIT} \
    && /root/Development/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /work/orkige
CMD ["/bin/bash"]
