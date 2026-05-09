# Patches Applied to the Amazon KVS WebRTC SDK

This directory holds the platform patches that are applied on top of the upstream
[Amazon Kinesis Video Streams WebRTC SDK C](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c)
(included as a submodule at `amazon-kinesis-video-streams-webrtc-sdk-c/`) to make
it work cleanly in the ESP-IDF / FreeRTOS / lwIP environment and to add a few
features that ESP-targeted use cases need but the upstream SDK does not yet ship.

Each patch falls into one of two categories:

1. **Aligned with upstream** — there is (or will be) a corresponding pull request
   on `awslabs/amazon-kinesis-video-streams-webrtc-sdk-c`. When that PR lands, the
   matching local patch can be removed from this directory.
2. **ESP-IDF platform-specific** — the change is specific to ESP-IDF /
   FreeRTOS / lwIP and is not a candidate for upstream. These patches stay
   long-term.

Patches are applied via:

```bash
cd amazon-kinesis-video-streams-webrtc-sdk-c
git am ../patches/*.patch
cd ..
```

## Patch ledger

| # | Patch | Category | Upstream PR / status |
|---|-------|----------|----------------------|
| 0001 | Added support for SDP re-negotiation flow | aligned | [awslabs#2214](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/pull/2214) |
| 0002 | SDP renegotiation: apply remote offer to transceiver directions and mark removed tracks inactive | aligned | [awslabs#2214](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/pull/2214) |
| 0003 | Fix format specifiers for cross-platform compatibility | aligned | test-only — track upstream |
| 0004 | Network.c: Changes to support ESP-IDF | ESP-specific | n/a |
| 0005 | ESP-IDF platform adaptations and robustness improvements | mixed | partial: [awslabs#2146](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/pull/2146) for the `PREFER_DYNAMIC_ALLOCS` portion |

> **Removed (absorbed upstream):**
>
> - Original `0004 — KVS SDK: Added support for dynamically adding ICE servers`
>   was absorbed by [awslabs#2164](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/pull/2164)
>   (`Feature: Dynamic ice server add`, commit `2641d6e5cd`). Consumer code
>   should call the upstream `peerConnectionUpdateIceServers` API directly.
> - `0006 — Fix bitwise & vs logical && typo in writeTransceiverDirection
>   cleanup` was absorbed by [awslabs#2278](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/pull/2278)
>   (commit `9b27f4b07`) and pulled into the submodule bump.

### Notes per patch

**0001 — SDP re-negotiation (basic flow).** Re-offer from the same peer reuses an
active session, or replaces a terminated one. Touches `PeerConnection.c` and
`SessionDescription.c` plus the corresponding test. Required by the Matter Camera
spec. Drops when **awslabs#2214** lands.

**0002 — SDP re-negotiation (transceiver directions).** Follow-up to 0001 that
applies the remote offer to transceiver directions and marks removed tracks
inactive. Drops together with 0001 once **awslabs#2214** lands.

**0003 — Format specifiers cross-platform compat.** Limited to
`tst/PeerConnectionFunctionalityTest.cpp`. Replaces non-portable format
specifiers; landing upstream is test-hygiene rather than a feature change.

**0004 — `Network.c` ESP-IDF adaptations.** Long-term ESP-IDF specific change
in `src/source/Ice/Network.c`. lwIP `getifaddrs` / route enumeration semantics
differ from glibc, and this patch closes that gap. Stays.

**0005 — ESP-IDF platform adaptations + robustness.** The largest patch, mixing
two kinds of changes:

  - **Aligned-with-upstream:** the `PREFER_DYNAMIC_ALLOCS` /
    `DYNAMIC_SIGNALING_PAYLOAD` / `USE_DYNAMIC_URL` paths replace huge static
    arrays in the signaling payload and TURN URL fields with optional dynamic
    allocation. This is the same idea as **awslabs#2146** ("Option to use
    dynamic allocations over huge static arrays — limits memory utilization").
    When #2146 lands upstream, this portion of 0005 will be split out and
    removed.
  - **ESP-IDF-specific (stays):** ConnectionListener custom thread for
    constrained-stack platforms, mbedtls-3.x compatibility shims, robustness
    fixes in `SocketConnection.c`, `Sctp.c` and `LwsApiCalls.c`.

Patch 0005 will be split into a `0005a-dynamic-allocs` (aligned) and a
`0005b-esp-platform` (ESP-specific) once #2146 reaches upstream review.

## Patches-removal workstream

The "aligned" patches above represent local divergence from upstream. A clean
end state is:

```
patches/
├── 0001-Network.c-...                     # ESP-specific (was 0004)
├── 0002-ConnectionListener-mbedtls-...    # ESP-specific (was 0005b)
└── README.md
```

The path to that state is the upstream PR landing schedule:

| Local patch | Removed when |
|-------------|--------------|
| 0001, 0002, 0003 | `awslabs#2214` (and its companion #2156 if applicable) merges |
| 0005 (aligned half) | `awslabs#2146` merges |

This file should be kept up-to-date as patches are added, removed, or split.
