# RFC: libwebsockets Linux IDF target support upstream to `esp-protocols`

This is a draft RFC issue ready to paste at
<https://github.com/espressif/esp-protocols/issues/new>. Title:
**"libwebsockets: support the IDF Linux target (`idf.py --preview set-target linux`)"**

---

## Why

The `kvs_signaling` component in
[amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port][port] uses
libwebsockets for its signaling client. To run our integration tests
without QEMU on the GitHub Actions runner — and to give application
developers a fast iteration loop — we build that whole stack against
the **IDF Linux target** (`idf.py --preview set-target linux`).

The `esp-protocols` libwebsockets component is great for ESP targets
but doesn't currently build under `--set-target linux`: lws's
`CMakeLists-implied-options.txt` force-enables `LWS_PLAT_FREERTOS` whenever
`ESP_PLATFORM` is defined, which is also the case for IDF's Linux
target — but on Linux the FreeRTOS plat sources pull in IDF's
`portmacro_idf.h` / `pthread.h` shims that don't compile against
glibc/musl. The result is a build break before any code in our SDK is
touched.

Our local fork
([components/libwebsockets/CMakeLists.txt][cmake]) papers over this
with ~80 lines of CMake — the additions are purely behind
`if(IDF_TARGET STREQUAL "linux")` branches and don't affect the ESP
build path at all. We'd love to upstream them so the registry
component just works on both platforms.

[port]: https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port
[cmake]: https://github.com/vikramdattu/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/blob/feat/docker-integration-test/components/libwebsockets/CMakeLists.txt

## What needs to change

All Linux-only, additive on top of the existing ESP code path:

| Block | Reason |
|---|---|
| `if(IDF_TARGET STREQUAL "linux")` branch | Gate everything below |
| `set(LWS_WITH_ESP32 OFF CACHE BOOL "" FORCE)` and `set(LWS_PLAT_FREERTOS OFF ...)` | Force lws to use `plat/unix` instead of FreeRTOS shims |
| `set(LWS_WITH_MBEDTLS ON ...)` | Lock down crypto backend to mbedtls (avoids find_library finding Homebrew openssl on macOS) |
| Pre-pin `LWS_MBEDTLS_LIBRARIES` and `LWS_MBEDTLS_INCLUDE_DIRS` to IDF's mbedtls component | Otherwise `find_library(MBEDTLS_LIBRARY mbedtls)` finds `/opt/homebrew/lib/libmbedtls.dylib` (mbedtls 3.6.4) on macOS hosts — that has a different config than IDF's mbedtls and breaks `createRtcCertificate()` at runtime with `0x59000001` |
| `set(LWS_WITHOUT_TESTAPPS ON)`, `set(LWS_WITH_MINIMAL_EXAMPLES OFF)` | Test apps + minimal examples don't link against IDF's static-archive set on Linux |
| `set(LWS_WITH_FILE_OPS ON ...)` | lejp-conf and secure-streams policy parsing reference `lws_open` which is itself only compiled under `LWS_WITH_FILE_OPS`. On real ESP targets these code paths are dead-stripped; on Linux they get compiled and need the symbol. |
| `set(LWS_WITH_SHARED OFF)`, `set(LWS_WITH_STATIC ON)` | Linux upstream lws builds both shared + static by default; the shared `libwebsockets.19.dylib` link step pulls in IDF's main archive looking for `_app_main` (defined in our example main.c), which fails. Static-only matches IDF's link semantics. |
| `set(LWS_WITHOUT_EXTENSIONS ON)`, `set(LWS_WITH_ZLIB OFF ...)` | `CMakeLists-implied-options.txt` does an unconditional `set(LWS_WITH_ZLIB 1)` whenever extensions are enabled. ZLIB requires `zlib1g-dev` on Debian; the espressif/idf:release-v5.5 base image doesn't ship it, so `find_package(ZLIB REQUIRED)` fails at configure time on CI. |
| Filter lwip include dirs from the `websockets` target's `INCLUDE_DIRECTORIES` | On Linux, `CONFIG_LWIP_ENABLE=n` so lwip provides no headers, but the component's `port/include` directory still propagates and shadows host's `<netinet/in.h>` with a wrapper that forwards to `lwip/inet.h` (which isn't on the path). |
| Add `-Wno-error=undef` | lws's mbedtls wrapper sources use bare `#if CONFIG_IDF_TARGET_*` patterns — fine on real ESP targets where the macros are 0/1, but on the Linux IDF target none of `CONFIG_IDF_TARGET_*` are defined and lws compiles with `-Werror=undef`. |

The ESP target path is **completely unchanged** by these blocks (they
only fire under `if(IDF_TARGET STREQUAL "linux")`).

## Verification

Locally, with these blocks, both targets build and pass tests:

- `cd examples/webrtc_classic && idf.py set-target esp32s3 && idf.py build` — green
- `cd examples/linux_test && idf.py --preview set-target linux && idf.py build` — green
- The Linux build runs end-to-end against a real KVS signaling channel + aiortc Python viewer, recording a bit-exact H.264 stream the master sent.

## What about lws version?

The `esp-protocols` libwebsockets component pins lws v4.3.3~1; we pin
v4.4.0. Our one historical patch (a CMake fix to skip the FreeRTOS
plat on Linux) is already in lws upstream at our pinned SHA, so we
don't need to apply patches at build time anymore. Bumping
`esp-protocols`'s submodule to v4.4.0 in the same PR (or a sibling)
would let us drop the `patches/` apply logic entirely.

## Proposed plan

1. RFC discussion (this issue) — alignment on whether you want this in
   the registry component, or if you'd prefer a separate Linux-target
   component, or to keep it out of scope.
2. PR adding the CMake blocks above, gated on `IDF_TARGET STREQUAL "linux"`.
3. Optional sibling PR bumping lws SHA from v4.3.3 to v4.4.0 (matches
   what we ship).

Once shipped, our local fork in
`components/libwebsockets/` becomes a `path: → registry:` swap in
`idf_component.yml`. We're happy to do the PR work; just want to confirm
the direction first.

## Stretch: usrsctp + libsrtp2

Same playbook would apply to the other two WebRTC building blocks our
port carries: `esp_usrsctp` and `libsrtp2`. Both are in our fork as
ESP-IDF components today (with similar Linux-target tweaks). Open to
upstream-donating those too if you'd accept.
