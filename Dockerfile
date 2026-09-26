# Use a robust base for toolchain setup
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Dependencies, GCC 13 (the default c++) and Clang 19 (C++23 support).
#    Clang 19 comes from Ubuntu's own archive: apt.llvm.org's install script
#    is often unreachable from CI runners and then fails the whole image build.
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
    clang-19 \
    lld-19 \
    libclang-rt-19-dev \
    clang-format-19 \
    && ln -sf /usr/bin/clang-19 /usr/bin/clang \
    && ln -sf /usr/bin/clang++-19 /usr/bin/clang++ \
    && ln -sf /usr/bin/clang-format-19 /usr/bin/clang-format \
    && pip3 install --break-system-packages cmakelang \
    && apt-get clean

# 2. Install Emscripten (EMSDK) for WASM support, pinned so CI builds with the
#    SDK the WASM tests were verified against (bump deliberately; keep
#    .github/workflows/deploy-demo.yml in sync).
ARG EMSDK_VERSION=4.0.23
WORKDIR /opt
RUN git clone https://github.com/emscripten-core/emsdk.git
WORKDIR /opt/emsdk
RUN ./emsdk install ${EMSDK_VERSION} \
    && ./emsdk activate ${EMSDK_VERSION}

# 3. System Boost.Context for the install/find_package packaging check
#    (a separate layer so changing it does not rebuild the SDK layers above)
RUN apt-get update && apt-get install -y libboost-context-dev \
    && apt-get clean

# 4. Environment Setup
ENV EMSDK=/opt/emsdk
ENV EM_CONFIG=/opt/emsdk/.emscripten
ENV PATH="/opt/emsdk:/opt/emsdk/upstream/emscripten:${PATH}"

WORKDIR /workspace
