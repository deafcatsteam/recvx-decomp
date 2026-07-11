FROM mcr.microsoft.com/devcontainers/cpp:2-debian13

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="none"

# Install MIPS
RUN apt-get update \
    && apt-get install -y binutils-mips-linux-gnu binutils-mipsel-linux-gnu

# Download the wibo decompilation tool, make it executable, and move it to /usr/bin.
RUN wget https://github.com/decompals/wibo/releases/download/1.0.0/wibo-x86_64 \
    && chmod +x wibo-x86_64 \
    && sudo mv wibo-x86_64 /usr/bin/wibo

# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# [Optional] Uncomment this section to install additional packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>

# Linux build tooling for the PC port (Phase 0: Ninja + vcpkg x64-linux)
RUN apt-get update \
    && apt-get install -y \
        ninja-build nasm build-essential \
        autoconf automake autoconf-archive libtool m4 bison flex \
        python3-venv python3-pip gperf \
        curl zip unzip tar pkg-config \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libgl1-mesa-dev libasound2-dev libpulse-dev

# vcpkg, pinned to a fixed path so CMakePresets.json can reference it via VCPKG_ROOT
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg
