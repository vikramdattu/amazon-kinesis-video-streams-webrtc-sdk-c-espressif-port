# Linux integration test

This example is built **only for the Linux IDF target** and is the canary used
by `tests/docker/` to drive the espressif-port KVS WebRTC SDK end-to-end as a
master peer, against the upstream `kvsWebRTCClientViewer` sample running as
the viewer.

It deliberately does **not** depend on `media_stream` or
`network_coprocessor` (those are ESP-only); media is read from the
pre-recorded H.264 + Opus sample frames that ship with the upstream KVS SDK
submodule (`amazon-kinesis-video-streams-webrtc-sdk-c/samples/`).

## Build

Requires ESP-IDF release/v5.5 or newer with the Linux-target preview enabled.

```bash
. $IDF_PATH/export.sh
cd examples/linux_test
idf.py --preview set-target linux
idf.py build
```

The build produces `build/linux_test` (a normal Linux ELF).

## Run

Runtime configuration via env vars:

```
AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY    real KVS credentials
AWS_DEFAULT_REGION                          e.g. us-west-2
KVS_CHANNEL_NAME                            existing signaling channel name
```

Direct invocation:

```bash
AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... AWS_DEFAULT_REGION=us-west-2 \
KVS_CHANNEL_NAME=esp-port-it-test ./build/linux_test
```

For the full integration test (master + viewer in containers, recording +
verification of received media), see [`../../tests/docker/`](../../tests/docker/).

## Status

- **Phase 1 (canary):** boots, prints env, exits 0 — proves the Linux build
  pipeline works for the SDK component graph.
- **Phase 1b (in progress):** real signaling + peer connection + sample-frame
  feed.
