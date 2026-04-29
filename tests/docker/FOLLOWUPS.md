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

## 7b. linux_test: random segfaults during signaling

**Status:** **resolved.** ASan caught the actual bugs — the various
unrelated-looking crashes were all symptoms of the same heap
corruption. Three real fixes in
`fix(linux_test): kill the heap corruption that masked everything else`:

1. **Static-task stack buffers were 8x too small** in
   `esp_work_queue_start` and `app_webrtc_run`'s `xTaskCreateStatic`
   calls. Upstream FreeRTOS Linux port has `StackType_t = unsigned
   long` (8 bytes); the IDF xtensa/rv32 ports have it as `uint8_t`.
   Caller must allocate `usStackDepth * sizeof(StackType_t)` bytes,
   not `usStackDepth` bytes — `prvInitialiseNewTask` clobbers ~224 KB
   past the end otherwise.
2. **KVS SDK pthread stacks were 16-48 KB** (RTOS-sized). Bumped to
   a 512 KB minimum on the Linux IDF target in
   `defaultCreateThreadPriWithCaps` to absorb 8-byte pointers / glibc
   / lws / ASan instrumentation expansion.
3. **`SignalingMessage` ABI mismatch.** Upstream's `Include.h` makes
   the `payload` field a pointer or an inline 18 KB array depending on
   `DYNAMIC_SIGNALING_PAYLOAD`; that macro was set in `kvs_utils`
   (consumer side) but not propagated to the upstream KVS SDK source
   files compiled by the `kvs_webrtc` and `kvs_signaling` components.
   Different TUs saw different struct layouts → SDK wrote JSON into
   `payload[]` and consumer read the corrupted "pointer" — the
   first 7 bytes of the JSON spelled `{"candi`. Fixed by pinning
   `-DDYNAMIC_SIGNALING_PAYLOAD=1` PUBLICly in both component
   CMakeLists, plus an upstream KVS SDK patch
   (`patches/0008-...`) for the parseSignalingMessage MEMSET that
   zeroed the freshly-allocated payload pointer + the
   `SIZEOF(PCHAR)` cap that should have been
   `MAX_SIGNALING_MESSAGE_LEN + 1`.

Verified: `Sending answer to peer …` → ICE candidate gathering
finished → viewer reports `Track received` (video + audio) →
`Applied SDP_ANSWER` → `ICE connection state: completed`. DTLS
handshake completion + SRTP frame flow is the next gap.

## 7d. linux_test: ICE nomination times out → no media frames flow

**Status:** open. Symptoms:

- Master reaches `ICE_AGENT_STATE_CHECK_CONNECTION → CONNECTED → NOMINATING`
  for the offer/answer pair against the Python aiortc viewer.
- After 30 s `iceCandidateNominationTimeout`, master fails with
  `fromNominatingIceAgentState(): operation returned status code:
  0x5a000013` (`STATUS_ICE_FAILED_TO_NOMINATE_CANDIDATE_PAIR`).
- Master log is full of STUN binding error responses from peer:
  ```
  handleStunPacket(): Error STUN packet. Packet type: 0x111.
      0009000F 00000400 4261642052657175657374 ...
  handleStunPacket(): Error binding response! <local-ufrag> <remote-ufrag>
  ```
  ERROR-CODE 0x400 / "Bad Request" — the peer rejects each binding
  request. aiortc's `aioice.ice.request_received()`
  (`aioice/ice.py:1117`) returns 400 only on USERNAME mismatch or
  MESSAGE-INTEGRITY HMAC failure, so the integrity / username
  format on the master's binding request is not what aiortc
  expects.
- Viewer side reports `ICE connection state: completed` with
  `Track received: kind=video / kind=audio` — but those are the
  RTCRtpReceiver objects created by setRemoteDescription, not actual
  media frames over the wire. aiortc's "completed" likely reflects
  its own perspective (incoming binding requests succeeded), not
  master's.
- TURN-over-TLS works now (`patches/0009-...`), but the relay path
  has the same nomination problem, so this is upstream of TURN.

**Likely causes:**

1. ICE-CONTROLLED / ICE-CONTROLLING attribute mismatch in master's
   STUN binding request (aiortc `request_received()` checks role
   conflict at `aioice/ice.py:1121-1132`).
2. USERNAME format wrong:
   `<expected-by-aiortc>` is `<aiortc-ufrag>:<master-ufrag>`. KVS
   SDK should produce that, but the format may diverge under
   non-trickle vs trickle paths or for ufrag values containing
   special chars.
3. Master / viewer are both on the same macOS host with multiple
   active interfaces (en0 WiFi, utun*, en4) — host candidates from
   different subnets, source IP / port mismatch on send-to vs
   receive-from. macOS pf default-deny on inbound UDP per interface
   is also possible.

**Investigation steps:**

- Add `tcpdump -i any -w /tmp/master.pcap port 50000-65535` while
  the test runs; inspect a master→viewer STUN binding request and
  the matching 400 response in Wireshark to see which exact
  attribute aiortc rejected.
- Compare master's STUN attribute set against
  `aioice.stun.parse_message`. Patch master's iceUtils.c if
  needed, or align role attributes.
- Try the same setup on a Linux host (no multi-interface, no pf):
  this is the configuration CI will actually run, and it may "just
  work" once we get past the macOS-host quirks.
- If the local-host setup proves too quirky, gate the Python
  viewer + master to run inside the same Docker network (the
  original intent — `tests/docker/docker-compose.yml`); local
  macOS run remains useful only up to the ICE-handshake green
  signal we already have.

## 7e. linux_test: `KVS_FRAMES_DIR` default is wrong when run from build dir

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
