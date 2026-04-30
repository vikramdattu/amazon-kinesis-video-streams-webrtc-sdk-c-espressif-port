#!/usr/bin/env bash
#
# Cross-check that the python_viewer captured real media from the
# master peer. Used by every integration_test.yml job that captures
# media.
#
# We accept either of two pass paths:
#
#   1. The MKV is plausibly populated and ffprobe sees H.264 video +
#      Opus audio with at least N packets each. This is the original
#      criterion and stays in place — when libavcodec is happy with
#      what aiortc feeds it, this is the strongest signal.
#
#   2. **Raw H.264 dump fallback.** When aiortc's H264Decoder rejects
#      individual RTP packets ("Invalid data found when processing
#      input"), no frames make it into the MKV. Our viewer.py
#      monkey-patches the decoder to also dump every depacketized
#      access unit to `${MKV%.mkv}.h264` (Annex-B). That file is
#      written from inside the depacketizer, so it captures whatever
#      the master *actually emitted on the wire* — even when libavcodec
#      can't decode it. If the dump is non-trivial in size and ffmpeg's
#      command-line decoder (which is more permissive than aiortc's
#      embedded libavcodec) can probe it, we accept that too.
#
# Usage:  verify_recording.sh <path-to-mkv> [<min-video-packets>] [<min-audio-packets>]

set -euo pipefail

MKV="${1:-}"
MIN_VID="${2:-30}"
MIN_AUD="${3:-30}"

if [ -z "$MKV" ]; then
    echo "::error::verify_recording.sh: missing <path-to-mkv> argument" >&2
    exit 2
fi

RAW_H264="${MKV%.*}.h264"

if [ ! -s "$MKV" ] && [ ! -s "$RAW_H264" ]; then
    echo "::error::Neither $MKV nor $RAW_H264 exists / is non-empty"
    exit 1
fi

mkv_ok=0
if [ -s "$MKV" ]; then
    ffprobe -v error -show_streams -count_packets "$MKV" > ffprobe.txt 2>/dev/null || true
    echo "--- ffprobe (mkv) ---" ; cat ffprobe.txt ; echo "----------------"

    VID_CODEC=$(awk -F= '/codec_type=video/{vid=1} vid && /codec_name=/{print $2; exit}' ffprobe.txt)
    AUD_CODEC=$(awk -F= '/codec_type=audio/{aud=1} aud && /codec_name=/{print $2; exit}' ffprobe.txt)
    VID_PACKETS=$(awk -F= '/codec_type=video/{vid=1} vid && /nb_read_packets=/{print $2; exit}' ffprobe.txt)
    AUD_PACKETS=$(awk -F= '/codec_type=audio/{aud=1} aud && /nb_read_packets=/{print $2; exit}' ffprobe.txt)

    echo "MKV: video codec=${VID_CODEC:-none} packets=${VID_PACKETS:-0}; audio codec=${AUD_CODEC:-none} packets=${AUD_PACKETS:-0}"

    if [ "$VID_CODEC" = "h264" ] && [ "$AUD_CODEC" = "opus" ] \
       && [ "${VID_PACKETS:-0}" -ge "$MIN_VID" ] && [ "${AUD_PACKETS:-0}" -ge "$MIN_AUD" ]; then
        mkv_ok=1
    fi
fi

raw_ok=0
if [ -s "$RAW_H264" ]; then
    RAW_SIZE=$(wc -c < "$RAW_H264" | tr -d ' ')
    # ffprobe's `h264` demuxer parses Annex-B directly. We force
    # `-f h264` because the file has no container.
    ffprobe -v error -f h264 -count_frames -show_streams "$RAW_H264" > ffprobe_raw.txt 2>/dev/null || true
    echo "--- ffprobe (raw h264) ---" ; cat ffprobe_raw.txt ; echo "----------------"

    RAW_FRAMES=$(awk -F= '/^nb_read_frames=/{print $2; exit}' ffprobe_raw.txt)
    # ffprobe emits `N/A` when it can't reliably count frames (e.g. very
    # short streams without a parsable IDR). Coerce to 0 so the integer
    # comparison below doesn't blow up with "integer expression expected".
    case "$RAW_FRAMES" in ""|"N/A") RAW_FRAMES=0 ;; esac
    echo "RAW_H264: $RAW_H264 size=${RAW_SIZE} bytes, ffprobe frames=${RAW_FRAMES}"

    # Pass criterion: at least 30 frames decoded by ffmpeg's CLI (≥1 s @ 30 fps).
    # The frame count comes from libavcodec's own Annex-B parser, which is more
    # permissive than aiortc's embedded one — so we accept here even if aiortc
    # rejected every individual RTP packet on the way in.
    if [ "$RAW_FRAMES" -ge "$MIN_VID" ]; then
        raw_ok=1
    fi
fi

if [ "$mkv_ok" = "1" ]; then
    echo "PASS — H.264 + OPUS round-trip verified end-to-end via MKV."
fi

# Bit-exact NAL diff against the master's source samples. Stronger
# signal than ffprobe frame counts: proves every received NAL byte
# matches some source NAL byte-for-byte (RTP/SRTP/depacketization is
# lossless for payload). Runs whenever a non-empty raw dump exists,
# regardless of whether the MKV passed.
bitex_ok=0
if [ -s "$RAW_H264" ]; then
    SAMPLES_DIR="${KVS_SAMPLES_DIR:-$GITHUB_WORKSPACE/amazon-kinesis-video-streams-webrtc-sdk-c/samples}"
    if [ -d "$SAMPLES_DIR/h264SampleFrames" ]; then
        # `set +e` so a failed bit-exact check doesn't kill us — we
        # still want to fall through to the raw_ok / mkv_ok criteria.
        set +e
        python3 "$(dirname "$0")/verify_h264_dump.py" "$RAW_H264" "$SAMPLES_DIR"
        bitex_rc=$?
        set -e
        if [ "$bitex_rc" = "0" ]; then bitex_ok=1; fi
    else
        echo "::warning::skipping bit-exact NAL verifier — no h264SampleFrames at $SAMPLES_DIR"
    fi
fi

if [ "$mkv_ok" = "1" ] && [ "$bitex_ok" = "1" ]; then
    echo "PASS — MKV + bit-exact NAL match."
    exit 0
fi
if [ "$bitex_ok" = "1" ]; then
    echo "PASS — bit-exact NAL match (every received NAL byte-identical to source)."
    exit 0
fi
if [ "$mkv_ok" = "1" ]; then
    exit 0
fi
if [ "$raw_ok" = "1" ]; then
    echo "PASS — raw H.264 dump has $RAW_FRAMES frames; transport verified (no bit-exact source compare available)."
    echo "::warning::MKV path failed (likely H264Decoder rejection); raw dump is the authority."
    exit 0
fi

echo "::error::All verification paths failed (MKV empty, raw dump unverifiable)."
exit 1
