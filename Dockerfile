# syntax=docker/dockerfile:1

# Static musl Linux release builder stage (CF-205)
FROM alpine:3.20 AS release-linux-musl

RUN apk add --no-cache \
    clang \
    lld \
    llvm \
    make \
    git \
    musl-dev \
    libc++-dev \
    libc++-static \
    libstdc++-dev \
    file \
    binutils \
    tar

WORKDIR /src/cfire
COPY Makefile VERSION ./
COPY src ./src
COPY runtime ./runtime
COPY pkg ./pkg

ARG GIT_SHA=dev
RUN make -j"$(nproc)" build CXX=clang++ GIT_SHA="${GIT_SHA}" \
    EXTRA_CXXFLAGS="-stdlib=libc++" \
    EXTRA_LDFLAGS="-static -stdlib=libc++ -fuse-ld=lld -lc++abi" \
    && file ./cfire | grep -qE "statically linked|static-pie" \
    && ! ldd ./cfire 2>&1 | grep -q "=> /" \
    && ./cfire install --portable /out/cfire \
    && [ -z "$(find /out/cfire \( -name '*.pch' -o -name '*.a' -o -name '*.o' \))" ] \
    && test -f /out/cfire/runtime/vendor/sqlite3/sqlite3.c \
    && test -f /out/cfire/runtime/vendor/simdjson/simdjson.cpp \
    && test -f /out/cfire/runtime/cfire-runtime.version

# Default Ubuntu runtime & development container image
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install ISO C++23 compiler (Clang), build tools, and native core:* libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    make \
    cmake \
    pkg-config \
    git \
    curl \
    ca-certificates \
    libsimdjson-dev \
    libzstd-dev \
    libsqlite3-dev \
    libssl-dev \
    libasound2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/cfire
COPY Makefile VERSION ./
COPY src ./src
COPY runtime ./runtime
COPY pkg ./pkg
COPY examples ./examples

# Compile and install cfire + runtime headers globally, warm C++23 PCH, and verify with cfire doctor
RUN make install PREFIX=/usr/local \
    && mkdir -p /workspace \
    && cp -R examples /workspace/examples \
    && cd /workspace \
    && rm -rf /src/cfire \
    && cfire setup --yes

WORKDIR /workspace
ENTRYPOINT ["cfire"]
CMD ["doctor"]
