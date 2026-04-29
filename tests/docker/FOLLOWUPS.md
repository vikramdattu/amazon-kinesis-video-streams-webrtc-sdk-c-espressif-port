# QEMU Integration Test — Follow-ups

This document journals known limitations and not-yet-fixed issues uncovered while bringing up the docker-based QEMU integration test for `webrtc_classic`. Each item below is a separate, scoped piece of work.

## 1. Master treats TURN failure as fatal in non-trickle ICE mode

**Status:** real product issue — affects real hardware too, not just QEMU.
**Athena:** task #14.

In **non-trickle ICE mode**, `kvs_webrtc.c:1041` (handler `onIceCandidateHandler`) sends the SDP_ANSWER only when ICE gathering signals "done" (called with `candidateJson==NULL`). If TURN allocation fails, the underlying KVS `IceAgent` may drive the session to FAILED without ever firing the gather-done callback. Result:

- `Non-trickle ICE mode: Answer will be sent after candidate gathering completes` is logged
- TURN_STATE_FAILED → ICE_AGENT_FAILED → peer state DISCONNECTED
- `Sending non-trickle answer with all candidates included` is **never** logged
- The remote viewer waits forever for the SDP_ANSWER and times out

**Fix direction:** treat TURN allocation failures as benign. Even in non-trickle mode the master should send the answer with the candidates it has gathered (host + srflx if available), or after a small "best-effort" timeout (e.g. 3s). TURN is a fallback; absence of a relay candidate must not block the basic offer/answer exchange.

**Where to patch:** likely `kvs_webrtc.c` non-trickle answer-send path (line 1041), and/or the upstream `IceAgent` state machine in `amazon-kinesis-video-streams-webrtc-sdk-c/src/source/Ice/` so that gathering completion fires when *any* path settles, not only on full success.

## 2. TURN GET_CREDENTIALS_FAILED — QEMU/slirp specific

**Status:** environmental, not a TURN-itself or SDK bug. TURN works fine
in production deployments (verified via the same KVS C SDK shipping in
esp-rainmaker, and rmaker-cli's aiortc viewer talking to real devices).
The failure is specific to our QEMU + slirp networking layer.
**Athena:** task #13.

`STATUS_TURN_CONNECTION_GET_CREDENTIALS_FAILED (0x5a00002c)` is **not** an auth-rejection error code. It is a **state-machine timeout** raised at `TurnConnectionStateMachine.c:280`:

```c
CHK(currentTime <= pTurnConnection->stateTimeoutTime,
    STATUS_TURN_CONNECTION_GET_CREDENTIALS_FAILED);
```

The master enters `TURN_STATE_GET_CREDENTIALS`, sends an `Allocate` request, and expects a `401 Unauthorized` response (which carries the realm + nonce) within `stateTimeoutTime`. If no response arrives, the state-machine times out and emits this error.

In our QEMU run the master logs show 4 retries of `TURN Get Credentials` (312–682 ms) followed by `TURN allocation` retries (459–904 ms) before the final `TURN_STATE_FAILED`. So packets are flowing in *some* direction but the auth handshake never completes.

**Likely causes:**

- Slirp asymmetric NAT (master sends from one source port, KVS replies to a different stable 5-tuple, slirp drops it).
- TLS-over-TCP TURN handshake timing out (KVS uses port 443 for both `turn:?transport=udp` and `turns:?transport=udp/tcp`).
- DNS resolution latency (KVS TURN servers use FQDN like `52-33-13-236.t-0b38280f.kinesisvideo.us-west-2.amazonaws.com` — slirp resolves via host, may have transient failures).

**Investigation steps:**

- Run with `qemu-system-xtensa ... -nic user,model=open_eth,id=net0 -object filter-dump,id=f1,netdev=net0,file=/tmp/qemu.pcap` and inspect the pcap to confirm whether KVS responses arrive and which 5-tuple they target.
- Bump `stateTimeoutTime` and see if the handshake just needs more time on slirp.
- Try the same run with bridged networking (tap+veth) — if it works there, confirms slirp path.

**Note**: `DEFAULT_TURN_GET_CREDENTIAL_TIMEOUT` and
`DEFAULT_TURN_ALLOCATION_TIMEOUT` are both 5s in the upstream SDK
(`TurnConnection.h`). On real networks that's plenty; under slirp the
NAT response apparently doesn't make the round trip in time.

## 3. Actual RTP media flow over slirp

**Status:** open. Required for the integration test to verify ffprobe-decodable frames.
**Athena:** task #15.

After the signaling-layer fixes (commits `bcd25a9` etc.) the master sends SDP_ANSWER, the viewer applies it, and tracks are negotiated. But the master's peer connection still goes FAILED → DISCONNECTED, so no RTP packets flow and `qemu_recording.mkv` is empty.

The remaining gap is purely **network reachability**:

- Master gathers a host candidate `10.0.2.15:N` (slirp internal IP).
- Viewer (running on host) cannot send UDP back to `10.0.2.15` without QEMU port-forwarding.
- TURN is disabled (see follow-up #2 above).

**Fix directions:**

- (a) Add `-nic user,model=open_eth,hostfwd=udp::PORT-:PORT` rules so the viewer can send UDP packets back into QEMU on a chosen relay port. Requires the master to advertise a candidate the host viewer can actually dial.
- (b) Bridge QEMU via tap+veth on Linux CI (won't work on macOS host directly).
- (c) Run the viewer **inside** the same network namespace (e.g. another QEMU instance, or sibling Docker container with a routable veth).

(c) is what the Docker harness was originally designed for (`tests/docker/docker-compose.yml`). Once the master gets past signaling reliably, the docker path should work. Local-host testing on macOS is the awkward case.

## 4. Local QEMU build's broken AES emulation

**Status:** macOS-build-environment-specific, not a regression in our patch.
**Tracked separately**: not on Athena (build-env issue).

When building `espressif/qemu` from source on macOS with Homebrew toolchain (libgcrypt 1.12.1, gnutls 3.8.12, libslirp 4.9.1), the resulting `qemu-system-xtensa` reports `qemu-system-xtensa: warning: [AES] Error reading from GDMA buffer` at runtime. The IDF-shipped pre-built binary (built on Linux CI) does not have this issue.

Workaround: use IDF's shipped binary for esp32 path testing (no patch needed; default 4 MB PSRAM matches QEMU's `ssi_psram` default). For esp32s3 with our `size_mbytes=8` patch, wait for upstream MR `idf/qemu` !115 to merge and IDF to re-publish the qemu-xtensa tool.

## 5. ESP-IDF OpenETH driver allocates `emac` state via plain calloc

**Status:** workaround in place; upstream IDF could be hardened.
**Tracked separately:** could be a small upstream IDF MR.

`hw/eth/src/openeth/esp_eth_mac_openeth.c:367` uses plain `calloc(1, sizeof(emac_opencores_t))` which on a heap that has PSRAM enabled may return a PSRAM pointer. The driver's IRAM-attributed ISR then dereferences `emac->rx_task_hdl` — if this happens during a flash-op cache-disable window (which we exercise by routing media-stream reads through `flash_wrapper`), it triggers a `Cache disabled but cached memory region accessed` panic.

Our local workaround: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512` in `sdkconfig.defaults.qemu` so any allocation ≤512 bytes (including `emac_opencores_t`) goes to internal DRAM.

**Upstream fix:** change `calloc` → `heap_caps_calloc(1, sizeof(emac_opencores_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Same pattern likely applies to other ETH drivers (`emac_dma`, `emac_esp32`).

## 6. linux_test: srtp_init() returns non-zero on first run

**Status:** **resolved.** Root cause was `components/libsrtp2/port/config.h`
hardcoding `SIZEOF_UNSIGNED_LONG=4` (correct for 32-bit ESP targets,
wrong on x86_64/arm64 host where `unsigned long` is 8 bytes). Cipher
self-tests in `srtp_crypto_kernel_init()` therefore returned
`srtp_err_status_alloc_fail` / `_algo_fail` and `srtp_init()` returned
non-OK. Fixed by switching to the compiler-provided `__SIZEOF_LONG__`
builtin so the value is target-aware (expands to 4 on xtensa lx6/lx7
+ rv32, 8 on the host).

## 7a. linux_test: lws was linking against system mbedtls

**Status:** **resolved.** Upstream lws's `tls/mbedtls/CMakeLists.txt`
falls into `find_library(MBEDTLS_LIBRARY mbedtls)` whenever
`LWS_MBEDTLS_LIBRARIES` and `LWS_MBEDTLS_INCLUDE_DIRS` are unset. On
macOS that found Homebrew's `/opt/homebrew/lib/libmbedtls.dylib` (3.6.4);
on a Debian CI host it would find `libmbedtls.so` from the apt package.
The dylib was propagated via lws's PUBLIC link interface and ended up
ahead of IDF's `libmbedcrypto.a` etc. in the link line, so symbol
resolution won from the system mbedtls — which has a different config
(no `MBEDTLS_X509_CRT_WRITE_C` among others). At runtime
`createRtcCertificate()` failed with `0x59000001`
(`STATUS_CERTIFICATE_GENERATION_FAILED`) even though IDF's static
archives were present in the same link command.

Fixed by pre-pinning `LWS_MBEDTLS_LIBRARIES` / `LWS_MBEDTLS_INCLUDE_DIRS`
to the IDF mbedtls component in `components/libwebsockets/CMakeLists.txt`
before `add_subdirectory(libwebsockets)` is called.

## 7b. linux_test: master segfaults in `signalingMessageReceived` when ICE candidate arrives before SDP_OFFER

**Status:** open. Linux-target only. Localized.

End-to-end loop now runs as far as: master is connected to KVS WSS
signaling and is receiving messages from a Python aiortc viewer.
Per-run instrumentation (ESP_LOGI traces in
`components/app_webrtc/src/app_webrtc.c::signalingMessageReceived`)
shows the crash sequence is:

```
sigMsgRecv: enter type=2 peer=py-viewer-XXXXXXXX payload_len=140
sigMsgRecv: lock taken
sigMsgRecv: hashTableContains -> peerConnectionFound=0 hash=0xNN
ERROR: Segmentation Fault
  signalingMessageReceived + 1848
  signalingMessageReceivedWrapper + 288
  kvsMessageReceivedCallback + 492
  receiveLwsMessageWrapper + 328
```

i.e. a `WEBRTC_MESSAGE_TYPE_ICE_CANDIDATE` (`type=2`) arrives **before**
the matching `OFFER` for the same peer (`peerConnectionFound=FALSE`).
The ICE-candidate branch (line 1347 in `app_webrtc.c`) walks into
`getPendingMessageQueueForHash()` / `createMessageQueue()` /
`stackQueueEnqueue()` / `MEMCALLOC(webrtc_message_t)` /
`MEMCPY(payload)`. A NULL deref or bad pointer somewhere on that path
is the cause.

This race is real: aiortc's viewer sends the offer and the trickled
ICE candidates back-to-back (`Sent SDP_OFFER`, then `Sent 12 trickled
ICE candidate(s)` within a few ms). KVS signaling channel ordering
isn't strictly preserved end-to-end, so the master can see one of the
candidates first. The master logs only show the first crash — but at
the same wall-clock the offer arrives in a separate WSS frame too.

The viewer worked previously against ESP-target masters, so this
suggests the Linux-target build of `signalingMessageReceived` is
missing some init or has a struct-layout / alignment difference vs the
ESP build, surfacing only when an out-of-order ICE candidate is
processed first.

**Earlier (less instrumented) crash signature:** `kvs_pc_send_message
+ 1088` ← `signalingMessageReceived + 2972`. Same function, deeper
offset; that run hit the OFFER-first ordering, which dispatches
through `pc_interface->send_message` → `kvs_handleOffer`. Both
ordering races crash.

**Investigation steps:**

- Build with `-fsanitize=address -fsanitize=undefined` and re-run.
  Capture the actual deref'd address + which line.
- Add ESP_LOGI traces between every line in the ICE_CANDIDATE branch
  (started; reverted before pushing).
- Cross-check `app_webrtc.c::signalingMessageReceived` for any field
  that's only initialised on the OFFER path (`pAppWebRTCSession` is
  NULL when peerConnectionFound=FALSE — confirm none of the deref'd
  fields touch it before allocation).
- Compare against an ESP-target run with the same aiortc viewer to
  see if ESP-target accepts an out-of-order candidate without
  crashing (it should — `peerConnectionFound=FALSE` falls into
  the queue path).

## 7c. linux_test: `KVS_FRAMES_DIR` default is wrong when run from build dir

**Status:** trivial; ergonomic. The default path `samples/h264SampleFrames`
is relative to cwd. Running `./build/linux_test.elf` from
`examples/linux_test/` therefore can't find the upstream KVS sample
frames at `${KVS_SDK_PATH}/samples/h264SampleFrames`. Worked-around by
exporting `KVS_FRAMES_DIR=…/samples` before running. Either change the
default to `${KVS_SDK_PATH}/samples` (resolved at build time) or document
it more loudly in `examples/linux_test/README.md`.

## 8. KVS C SDK `Include.h` documents `0x5a00002c` as `STATUS_TURN_CONNECTION_GET_CREDENTIALS_FAILED`

**Status:** documentation/quality-of-life. Not blocking.

The error code name suggests an authentication failure, but the actual emission point (state-machine timeout) is a network-response timeout. Renaming to `STATUS_TURN_GET_CREDENTIALS_TIMEOUT` (or adding a sibling timeout-specific status) would make logs much easier to triage.
