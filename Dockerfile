# syntax=docker/dockerfile:1
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
