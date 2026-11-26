# Name:        tools/linux/Dockerfile
# Description: the Linux package's build machine: Ubuntu 22.04 (glibc 2.35: the program runs on that and newer),
#              its compiler and CMake, SDL2 built from its release (the package carries it, as the Windows one
#              carries SDL2.dll), ffmpeg for the recordings' check
# Version:     0.1.0
# Created:     26-11-2026
#
# license:BSD-3-Clause
# Made and used by tools/make_release.ps1 -Linux (or by hand: docker build -t makaimura-linux tools/linux). SDL2
# loads X11, Wayland, PulseAudio, PipeWire and ALSA as it runs (SDL's own default), so the player's system needs
# only the ones it uses.
FROM ubuntu:22.04

ARG SDL2_VERSION=2.30.9
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake python3 curl ca-certificates pkg-config file binutils ffmpeg \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxinerama-dev libxss-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev \
        libgles2-mesa-dev libdrm-dev libgbm-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libdbus-1-dev \
        libudev-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sSL -o /tmp/sdl2.tar.gz \
        https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz \
    && tar -xzf /tmp/sdl2.tar.gz -C /tmp \
    && cmake -S /tmp/SDL2-${SDL2_VERSION} -B /tmp/sdl2-build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl2 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
    && cmake --build /tmp/sdl2-build -j"$(nproc)" \
    && cmake --install /tmp/sdl2-build \
    && cp /tmp/SDL2-${SDL2_VERSION}/LICENSE.txt /opt/sdl2/LICENSE.txt \
    && rm -rf /tmp/sdl2.tar.gz /tmp/SDL2-${SDL2_VERSION} /tmp/sdl2-build
