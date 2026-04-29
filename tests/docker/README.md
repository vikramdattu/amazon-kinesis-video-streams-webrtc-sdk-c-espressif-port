# Docker integration tests

End-to-end media flow over real KVS signaling. Two containers (master +
viewer) connect to the same KVS channel, exchange offer/answer, complete
ICE/DTLS, and stream a hard-coded H.264 + Opus loop for
`TEST_DURATION_SEC` seconds. `verify.sh` greps both logs for the markers
that prove a working session.

## Phase 1 scope

Both containers run the **upstream** `kvsWebrtcClientMaster` /
`kvsWebrtcClientViewer` C samples, built from the
`amazon-kinesis-video-streams-webrtc-sdk-c/` submodule (which is upstream
+ all our patches applied). Phase 1b will replace the master container
with our in-tree `examples/linux_test/` once the rest of the SDK
(`kvs_signaling`, `kvs_webrtc`, `app_webrtc`) grows Linux-target build
paths. See `examples/linux_test/README.md` for the canary status.

## Local run

You need: Docker, Docker Compose v2, and an AWS account with KVS
permissions in the region you point at.

```bash
cd tests/docker
cp .env.example .env
$EDITOR .env             # fill in AWS creds + KVS_CHANNEL_NAME
./run_test.sh
```

The first build pulls the upstream KVS deps (websockets, openssl, srtp,
log4cplus) and takes ~10 minutes. Subsequent builds reuse the cached
layer and are fast.

Logs land in `out/master.log` and `out/viewer.log` for inspection.

## Files

| File | Purpose |
|------|---------|
| `Dockerfile` | Single image used by both services. Builds upstream KVS samples from the vendored submodule. |
| `docker-compose.yml` | Pair of services sharing the same image; `command:` selects master vs viewer. |
| `run_test.sh` | Loads `.env`, brings the stack up, tears it down, runs `verify.sh`. |
| `verify.sh` | Pure log-grep checks (signaling/ICE/DTLS markers). |
| `.env.example` | Placeholder env file. Real `.env` is gitignored. |

## CI

Wired up via `.github/workflows/integration_test.yml`. Repo secrets
populate `.env` at job start. See that workflow for the canonical
command sequence.

## Limitations / future work

- **Frame-count and ffprobe verification:** the upstream viewer doesn't
  dump received H.264 to disk. A small viewer-side patch (or swapping in
  `kvsWebrtcClientViewerGstSample` with a `filesink`) would let us run
  `ffprobe -show_streams out/master.mkv` for stricter validation. Phase 2.
- **ESP32-S3 in QEMU:** see `Dockerfile.s3-build` (Phase 2 tier 1) — boot
  smoke only today.
- **Browser+Playwright variant:** deferred. The C-sample viewer is
  enough to prove the SDK works end-to-end.
