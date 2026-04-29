# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Linux-target integration test: drives the espressif-port SDK as a KVS WebRTC
# master peer entirely from a Linux host, with no real camera/mic. Used by
# tests/docker/ for end-to-end signaling + media validation against the
# upstream kvsWebRTCClientViewer sample.

cmake_minimum_required(VERSION 3.16)

# Only build for the Linux IDF target. Other targets fail fast with a clear message.
if(NOT IDF_TARGET STREQUAL "linux")
    message(FATAL_ERROR
        "examples/linux_test only builds for IDF_TARGET=linux. "
        "Run: idf.py --preview set-target linux")
endif()

# Re-use the shared KVS SDK path helper. Sets KVS_SDK_PATH=<repo root> when not in env.
include(${CMAKE_CURRENT_SOURCE_DIR}/../app_common/set_kvs_sdk_path.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(linux_test)

# ESP-IDF's Linux-target toolchain config injects `-Wno-error=maybe-uninitialized`
# into every component's compile line. That's a GCC option name; Apple Clang
# rejects it under -Werror via -Wunknown-warning-option, breaking the macOS
# host build. CI on Ubuntu (GCC) accepts the original flag and never sees this.
# Silencing the diagnostic is the smallest fix that keeps both compilers happy.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    idf_build_set_property(COMPILE_OPTIONS "-Wno-unknown-warning-option" APPEND)
endif()
