# Customer branch tracking — espressif-port repo

Branch: `customer/camera_solution`
Mirror name on esp-rainmaker: `customer/camera_solution`
Purpose: customer-deliverable streaming-camera baseline. **Every new fix lands here first.**

Maintained as: **a single composite commit (merge) on top of the migration tip.** No diag-level granularity preserved on purpose — diagnostic instrumentation commits were squashed implicitly via the merge approach.

---

## Lineage

```
customer/camera_solution
  └─ 3b687e8  feat(customer): merge support/p4_eye_audio_board onto migration tip
      ├─ parent 1: 7bdf8f0 (migration tip — vikram_gh/feature/migration_from_beta_and_additional_setup)
      └─ parent 2: 99aa6a1 (support/p4_eye_audio_board tip)
```

## What's in the customer branch

**Foundation** (from migration tip `7bdf8f02`):
- Bumped submodules: amazon-kinesis-video-streams-webrtc-sdk-c → `7c4748b5`, **esp_hosted → `a21b81cd`** (CORRECTION — this carries the local cherry-pick of `perf(sdio): optionally allocate transport buffers from SPIRAM` on top of upstream `8610f4f5`; required by P4-EYE to avoid `sdio_mempool_create` assert at boot. Pushed to `gitlab:perf/sdio_psram_mempool` (MR #241) so a fresh customer clone resolves the submodule cleanly. The fix is gated by `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` which is set in `examples/camera/standalone/sdkconfig.defaults.esp32p4` on the rmaker side.)
- Patch series 0001..0007 (one absorbed and dropped — patches 0004 and 0006 are out, 0007 still local)
- All upstream-bound work: CHANGELOG, README, NOTICE, patches ledger, fork-only DROP BEFORE MERGE marker

**Layered streaming-camera work** (from support/p4_eye_audio_board):
- `feat(media_stream): add H.264 receive/render pipeline` — RX decode path
- `feat(webrtc_classic): wire video player receive path for P4-EYE`
- `fix(media_stream): P4-EYE display init + debug logs`
- `patches: force DEFAULT_H264_FMTP on both offer and answer (0007)` — the second still-local patch
- `feat(media_stream): show FPS overlay on the receive-side display`
- `diag` family (squashed into the merge): per-track send/rx/played fps, encoder pipeline counters, TX SPS profile log, TX IDR accounting, LCD refresh count via LVGL monitor_cb, first-inbound-frame log
- `perf(video_render): center-crop fast-path when src_w == dst_w`
- `perf(video_render): use esp_image_effects for I420 → RGB565 on the crop fast-path`
- `fix(video_render): emit RGB565 big-endian to match ST7789 byte order`
- `perf(video_render): tune LVGL task to actually deliver 30 fps to ST7789`
- `fix(kvs_media): align audio TX PTS with video on the same wall clock` — audio↔video clock unification
- `DROP BEFORE MERGE: docs: add quick-start guide for ESP32-P4 Function EV Board` — the customer-facing doc

## Submodule pointer decisions

Final state (CORRECTED):
- `amazon-kinesis-video-streams-webrtc-sdk-c` = `7c4748b5` (migration tip, upstream develop ≥ v1.18.1+)
- `components/esp_hosted` = `a21b81cd` (carries `perf(sdio): optionally allocate transport buffers from SPIRAM` on top of upstream `8610f4f5`). **Pushed to `gitlab:perf/sdio_psram_mempool` (MR #241)** so a fresh customer clone resolves the submodule cleanly. The cherry-pick is gated by Kconfig `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` which the rmaker-side `customer/camera_solution` enables in `examples/camera/standalone/sdkconfig.defaults.esp32p4` (`d0ba1932`).

Original baseline commit's message claimed we used `8610f4f5` — that was misleading; the recorded SHA was `a21b81cd` all along. The actual gap was the missing Kconfig flag on the rmaker side, which has been fixed.

## Decisions that lost commit-level history

- All 6 diag commits squashed into the merge (no bisect granularity, per user direction).
- The 31 commits that were already absorbed on the migration tip (by patch-id) are not re-applied — git's merge resolution handles this.
- 5 conflict-resolutions auto-resolved via `-X theirs` strategy on the merge (support's version wins where both differ).

## Validation status (must run before pushing to customer)

- [x] `examples/streaming_only/` builds clean on P4-EYE — **2.4 MB binary, 17% partition free** (2026-05-14)
- [x] `examples/signaling_only/` builds clean on C6 — **1.9 MB binary, 36% partition free** (2026-05-14, after `bsp_selector` C6-exclusion fix `7b87454`)
- [ ] Pair runs end-to-end on P4-EYE with the rmaker side `customer/camera_solution`
- [ ] Same pair runs on EV Function Board
- [ ] Customer doc `docs/customer_quickstart_ev_function_board.md` Roadmap section reflects this branch's actual surface (no MKV/esp-dl references)
- [ ] `patches/README.md` ledger reflects the trimmed series (0001, 0002, 0003, 0005, 0007 present; 0004 and 0006 absorbed)

## Pending follow-ups before customer share

1. Push esp_hosted `a21b81cd` (SPIRAM mempool fix) upstream to gitlab `perf/sdio_psram_mempool` OR confirm migration tip's `8610f4f5` doesn't need it. Re-pin if a new SHA emerges.
2. Update `docs/customer_quickstart_ev_function_board.md` Roadmap section — drop MKV/esp-dl items.
3. Build verify both examples on P4-EYE.
4. Push `customer/camera_solution` to vikram_gh fork.
5. Coordinate with the rmaker-side `customer/camera_solution` branch — both should be tagged together when shipping.

## Workflow

- New fixes land here FIRST. In-house branches (feat/p4eye_audio_board_support_v2, support/p4_eye_audio_board, etc.) pull in via cherry-pick if needed.
- When a fix is added to `customer/camera_solution`, update this log with the commit SHA + one-liner.
- Final delivery: tag the head as `customer/camera_solution/v1.0` (or similar) and provide that to the customer.

## Commit log going forward

| SHA | Date | Description |
|---|---|---|
| `3b687e8` | 2026-05-14 | feat(customer): initial baseline merge (support/p4_eye_audio_board onto migration tip 7bdf8f02) |
| `288df61` | 2026-05-14 | docs: add CUSTOMER_BRANCH_TRACKING.md ledger |
| `7b87454` | 2026-05-14 | fix(media_stream): exclude bsp_selector for esp32c6 — unblocks signaling_only build on C6 |
| | | _new fixes go here_ |
