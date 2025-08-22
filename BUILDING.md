# BUILDING

This project: **Restinio C Library**
Version: **0.0.1**

## Local build

```bash
# one-shot build + install
./build.sh install
````

Or run the steps manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc || sysctl -n hw.ncpu || echo 4)"
sudo cmake --install .
```


## Install dependencies (from `cmake.libraries`)



### Development tooling (optional)

```bash
sudo apt-get update && sudo apt-get install -y valgrind gdb python3 python3-venv python3-pip perl autoconf automake libtool
```



### asio

Clone & build:

```bash
git clone --depth 1 --branch asio-1-30-2 --single-branch "https://github.com/chriskohlhoff/asio.git" "asio"
cd asio/asio
./autogen.sh
cd ../../
mkdir -p build/asio
cd build/asio
../../asio/asio/configure --prefix=/usr/local
make -j"$(nproc)"
sudo make install
cd ../..
rm -rf asio
```


### expected-lite

Clone & build:

```bash
git clone --depth 1 "https://github.com/andycurtis-public/restinio.git" "restinio"
mkdir -p build/expected-lite
cd build/expected-lite
cmake ../../restinio/expected-lite
make -j"$(nproc)"
sudo make install
cd ../..
rm -rf restinio
```


### fmt

Clone & build:

```bash
git clone --depth 1 "https://github.com/andycurtis-public/restinio.git" "restinio"
mkdir -p build/fmt
cd build/fmt
cmake ../../restinio/fmt
make -j"$(nproc)"
sudo make install
cd ../..
rm -rf restinio
```


### restinio

Clone & build:

```bash
git clone --depth 1 --branch v.0.7.3-fork --single-branch "https://github.com/andycurtis-public/restinio.git" "restinio"
mkdir -p build/restinio
cd build/restinio
cmake ../../restinio/dev -DRESTINIO_SAMPLE=OFF -DRESTINIO_TEST=OFF -DRESTINIO_DEP_FMT=system -DRESTINIO_DEP_EXPECTED_LITE=system
make -j"$(nproc)"
sudo make install
cd ../..
rm -rf restinio
```


## Docker (optional)

```dockerfile
# syntax=docker/dockerfile:1
ARG UBUNTU_TAG=22.04
FROM ubuntu:${UBUNTU_TAG}

# --- Configurable (can be overridden with --build-arg) ---
ARG CMAKE_VERSION=3.26.4
ARG CMAKE_BASE_URL=https://github.com/Kitware/CMake/releases/download
ARG GITHUB_TOKEN

ENV DEBIAN_FRONTEND=noninteractive

# --- Base system setup --------------------------------------------------------
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    curl \
    wget \
    tar \
    unzip \
    zip \
    pkg-config \
    sudo \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Development tooling (optional)
RUN apt-get update && apt-get install -y \
    valgrind \
    gdb \
    python3 \
    python3-venv \
    python3-pip \
    perl \
    autoconf \
    automake \
    libtool \
 && rm -rf /var/lib/apt/lists/*

# --- Install CMake from official binaries (arch-aware) ------------------------
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
      x86_64) CMAKE_ARCH=linux-x86_64 ;; \
      aarch64) CMAKE_ARCH=linux-aarch64 ;; \
      *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;; \
    esac; \
    apt-get update && apt-get install -y wget tar && rm -rf /var/lib/apt/lists/*; \
    wget -q "${CMAKE_BASE_URL}/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.tar.gz" -O /tmp/cmake.tgz; \
    tar --strip-components=1 -xzf /tmp/cmake.tgz -C /usr/local; \
    rm -f /tmp/cmake.tgz

# --- Create a non-root 'dev' user with passwordless sudo ----------------------
RUN useradd --create-home --shell /bin/bash dev && \
    echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    mkdir -p /workspace && chown dev:dev /workspace

USER dev
WORKDIR /workspace

# --- Optional Python venv for tools ------------------------------------------
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --upgrade pip
ENV PATH="/opt/venv/bin:${PATH}"

# --- Build & install asio ---
RUN set -eux; \
  git clone --depth 1 --branch asio-1-30-2 --single-branch "https://github.com/chriskohlhoff/asio.git" "asio" && \
  cd asio/asio && \
  ./autogen.sh && \
  cd ../../ && \
  mkdir -p build/asio && \
  cd build/asio && \
  ../../asio/asio/configure --prefix=/usr/local && \
  make -j"$(nproc)" && \
  sudo make install && \
  cd ../.. && \
  rm -rf asio

# --- Build & install expected-lite ---
RUN set -eux; \
  git clone --depth 1 "https://github.com/andycurtis-public/restinio.git" "restinio" && \
  mkdir -p build/expected-lite && \
  cd build/expected-lite && \
  cmake ../../restinio/expected-lite && \
  make -j"$(nproc)" && \
  sudo make install && \
  cd ../.. && \
  rm -rf restinio

# --- Build & install fmt ---
RUN set -eux; \
  git clone --depth 1 "https://github.com/andycurtis-public/restinio.git" "restinio" && \
  mkdir -p build/fmt && \
  cd build/fmt && \
  cmake ../../restinio/fmt && \
  make -j"$(nproc)" && \
  sudo make install && \
  cd ../.. && \
  rm -rf restinio

# --- Build & install restinio ---
RUN set -eux; \
  git clone --depth 1 --branch v.0.7.3-fork --single-branch "https://github.com/andycurtis-public/restinio.git" "restinio" && \
  mkdir -p build/restinio && \
  cd build/restinio && \
  cmake ../../restinio/dev -DRESTINIO_SAMPLE=OFF -DRESTINIO_TEST=OFF -DRESTINIO_DEP_FMT=system -DRESTINIO_DEP_EXPECTED_LITE=system && \
  make -j"$(nproc)" && \
  sudo make install && \
  cd ../.. && \
  rm -rf restinio


# --- Build & install this project --------------------------------------------
COPY --chown=dev:dev . /workspace/restinio-c
RUN mkdir -p /workspace/build/restinio-c && \
    cd /workspace/build/restinio-c && \
    cmake /workspace/restinio-c && \
    make -j"$(nproc)" && sudo make install

CMD ["/bin/bash"]
```
