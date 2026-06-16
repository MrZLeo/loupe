FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    ca-certificates \
    ccache \
    clang \
    clang-format \
    clang-tidy \
    cmake \
    cppcheck \
    curl \
    doxygen \
    git \
    graphviz \
    make \
    ninja-build \
    python3 \
    tar \
    unzip \
    vim \
    wget \
    && rm -rf /var/lib/apt/lists/*
