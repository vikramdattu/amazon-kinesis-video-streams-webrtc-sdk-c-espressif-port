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
$EDITOR .env                    # fill in AWS creds + KVS_CHANNEL_NAME
./run_test.sh                   # default — C master + C viewer
./run_test.sh --python          # master + aiortc Python viewer (records MKV)
./run_test.sh --all             # master + both viewers
```

The first C-image build pulls the upstream KVS deps (websockets,
openssl, srtp, log4cplus) and takes ~10 minutes. The Python image
builds in ~1 minute. Subsequent builds reuse the cached layer.

By default, the script deletes the KVS channel after the test exits so
CI runs don't leak channels into your AWS account. Set `KEEP_CHANNEL=1`
in `.env` to keep it around (handy when iterating locally on a fixed
channel name). Cleanup uses the `aws` CLI; if it isn't installed the
step is a no-op with a warning.

Outputs land in `out/`:
- `master.log` — C master log
- `viewer.log` — C viewer log (default mode)
- `python_viewer.log`, `python_viewer.mkv` — Python viewer log + recording

## Two verification paths

The harness ships two viewers because they prove different things:

| Mode | Viewer | Verification | What it proves |
|------|--------|--------------|----------------|
| `--c` (default) | upstream `kvsWebrtcClientViewer` | log-grep over `viewer.log` | interop with the AWS C SDK — same code AWS uses for their own tests |
| `--python` | aiortc-based | `ffprobe` on `python_viewer.mkv` | media actually decodes; codec=h264; ≥30 frames |
| `--all` | both | both | safest — but slowest first build |

## Files

| File | Purpose |
|------|---------|
| `Dockerfile` | C master+viewer image. Builds upstream KVS samples from the vendored submodule. |
| `python_viewer/Dockerfile` | aiortc-based Python viewer image. |
| `python_viewer/viewer.py` | KVS signaling + aiortc peer + MediaRecorder. ~200 lines. |
| `docker-compose.yml` | Master + both viewer flavours. C is default; `--profile python` enables the Python viewer. |
| `run_test.sh` | `--c` / `--python` / `--all` mode switcher. |
| `verify.sh` | Log-grep + ffprobe checks; mode-aware. |
| `.env.example` | Placeholder env file. Real `.env` is gitignored. |

## CI

Wired up via `.github/workflows/integration_test.yml`. Repo secrets
populate `.env` at job start. See that workflow for the canonical
command sequence.

### Switching from static AWS keys to OIDC (TODO)

CI today passes `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` /
`AWS_SESSION_TOKEN` as repo secrets. KVS rejects STUN binding requests
from some IPs (e.g. GitHub-hosted runner Azure ranges) with
`403 Forbidden IP` when authenticated this way; that's how the
python_viewer hits `aioice.stun.TransactionFailed: 403 - Forbidden IP`.

Both the **KVS WebRTC C SDK** and **Producer SDK CPP** upstream CIs use
GitHub Actions OIDC to assume an IAM role instead — see e.g.
[awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/.github/workflows/ci.yml](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/blob/develop/.github/workflows/ci.yml):

```yaml
- name: Configure AWS Credentials
  uses: aws-actions/configure-aws-credentials@v4
  with:
    role-to-assume: ${{ secrets.AWS_ROLE_TO_ASSUME }}
    aws-region: ${{ secrets.AWS_REGION }}
```

Required setup on the AWS side:
- Create an IAM role with `kinesisvideo:*` (or scoped) permissions.
- Trust policy allowing GitHub Actions OIDC for this repo + branch.
- Store the role ARN in repo secret `AWS_ROLE_TO_ASSUME`, region in
  `AWS_REGION`.

Once configured, drop the static `AWS_*` env vars from
`integration_test.yml` and replace with the snippet above. KVS allows
role-assumed callers regardless of source IP, so the 403 path
disappears.

## Limitations / future work

- **ESP32-S3 in QEMU:** see `Dockerfile.s3-build` (Phase 2 tier 1) — build
  smoke only today; QEMU networked boot deferred.
- **Browser+Playwright variant:** deferred. The two viewers above (C +
  Python) cover the interop and recording axes.
