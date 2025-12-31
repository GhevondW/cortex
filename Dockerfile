# Use a robust base for toolchain setup
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Dependencies and Clang 19 (C++23 support)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    python3 \
    python3-pip \
    ninja-build \
    lsb-release \
    software-properties-common \
    gnupg \
    gdb \
    gdbserver \
    lldb \
    strace \
    && wget https://apt.llvm.org/llvm.sh \
    && chmod +x llvm.sh \
    && ./llvm.sh 19 \
    && apt-get install -y clang-format-19 \
    && ln -s /usr/bin/clang-19 /usr/bin/clang \
    && ln -s /usr/bin/clang++-19 /usr/bin/clang++ \
    && ln -s /usr/bin/clang-format-19 /usr/bin/clang-format \
    && pip3 install --break-system-packages cmakelang \
    && apt-get clean

# 2. Install Emscripten (EMSDK) for WASM support
WORKDIR /opt
RUN git clone https://github.com/emscripten-core/emsdk.git
WORKDIR /opt/emsdk
RUN ./emsdk install latest \
    && ./emsdk activate latest

# 3. Environment Setup
ENV EMSDK=/opt/emsdk
ENV EM_CONFIG=/opt/emsdk/.emscripten
ENV PATH="/opt/emsdk:/opt/emsdk/upstream/emscripten:${PATH}"

WORKDIR /workspace
