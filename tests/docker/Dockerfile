# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Single image used by both master and viewer compose services. The two
# services differ only in the binary they exec at startup. We build the
# upstream amazon-kinesis-video-streams-webrtc-sdk-c samples (with our
# espressif-port submodule HEAD checked out, i.e. patches already applied)
# so the container peer is *the same code* the device side links against.
#
# Phase 1b will swap the master container for our in-tree Linux build of
# examples/linux_test once kvs_signaling / kvs_webrtc / app_webrtc grow
# Linux-target conditionals (tracked separately in Athena).

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        pkg-config \
        libssl-dev \
        libcurl4-openssl-dev \
        liblog4cplus-dev \
        liblog4cplus-2.0.5 \
        curl \
        ffmpeg \
        bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Build context is the espressif-port repo root (see docker-compose.yml
# `context: ../..`). Copy only the submodule we need.
COPY amazon-kinesis-video-streams-webrtc-sdk-c/ /src/sdk/

WORKDIR /src/sdk/build

# CMake auto-fetches open-source deps to /src/sdk/open-source/ on first
# configure (BUILD_DEPENDENCIES=ON, the default). Long step — cached layer.
RUN cmake -DBUILD_SAMPLE=ON \
          -DBUILD_TEST=OFF \
          -DBUILD_PERF_TEST=OFF \
          -DBUILD_BENCHMARK=OFF \
          .. \
    && make -j"$(nproc)" kvsWebrtcClientMaster kvsWebrtcClientViewer

# So the dynamically-loaded open-source libs (websockets, srtp, kvsCommonLws,
# kvspicUtils) resolve at runtime without LD_LIBRARY_PATH gymnastics.
ENV LD_LIBRARY_PATH=/src/sdk/build:/src/sdk/open-source/lib

# Frame data lives at /src/sdk/samples/{h264,opus}SampleFrames — the master
# defaults to looking for them via SAMPLE_PRE_GENERATED_CERT or via working
# directory. We exec the binary from /src/sdk/build/samples so the relative
# `../samples/h264SampleFrames` path the master uses just works.
WORKDIR /src/sdk/build/samples

# `command:` in docker-compose.yml selects which binary runs.
ENTRYPOINT []
