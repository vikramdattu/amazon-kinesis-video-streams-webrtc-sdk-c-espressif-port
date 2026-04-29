#!/usr/bin/env bash
#
# Cross-check that the python_viewer's MKV recording contains real
# H.264 video + Opus audio frames received from the master peer.
# Used by every integration_test.yml job that captures media.
#
# Usage:  verify_recording.sh <path-to-mkv> [<min-video-packets>] [<min-audio-packets>]
#
# Defaults to ≥30 video and ≥30 audio packets (~1 s each), which keeps
# the bar low enough to absorb slow ICE/DTLS handshakes in CI but high
# enough to catch a fully-broken media path.

set -euo pipefail

MKV="${1:-}"
MIN_VID="${2:-30}"
MIN_AUD="${3:-30}"

if [ -z "$MKV" ]; then
    echo "::error::verify_recording.sh: missing <path-to-mkv> argument" >&2
    exit 2
fi

if [ ! -s "$MKV" ]; then
    echo "::error::$MKV missing or empty"
    exit 1
fi

ffprobe -v error -show_streams -count_packets "$MKV" > ffprobe.txt
echo "--- ffprobe ---" ; cat ffprobe.txt ; echo "----------------"

VID_CODEC=$(awk -F= '/codec_type=video/{vid=1} vid && /codec_name=/{print $2; exit}' ffprobe.txt)
AUD_CODEC=$(awk -F= '/codec_type=audio/{aud=1} aud && /codec_name=/{print $2; exit}' ffprobe.txt)
VID_PACKETS=$(awk -F= '/codec_type=video/{vid=1} vid && /nb_read_packets=/{print $2; exit}' ffprobe.txt)
AUD_PACKETS=$(awk -F= '/codec_type=audio/{aud=1} aud && /nb_read_packets=/{print $2; exit}' ffprobe.txt)

echo "video codec=${VID_CODEC:-none} packets=${VID_PACKETS:-0}"
echo "audio codec=${AUD_CODEC:-none} packets=${AUD_PACKETS:-0}"

ok=1
[ "$VID_CODEC" = "h264" ] || { echo "::error::video codec is '${VID_CODEC:-none}', expected h264"; ok=0; }
[ "$AUD_CODEC" = "opus" ] || { echo "::error::audio codec is '${AUD_CODEC:-none}', expected opus"; ok=0; }
[ "${VID_PACKETS:-0}" -ge "$MIN_VID" ] || { echo "::error::video packets ${VID_PACKETS:-0} < $MIN_VID"; ok=0; }
[ "${AUD_PACKETS:-0}" -ge "$MIN_AUD" ] || { echo "::error::audio packets ${AUD_PACKETS:-0} < $MIN_AUD"; ok=0; }

[ "$ok" = "1" ] || exit 1
echo "PASS — H.264 + OPUS round-trip verified end-to-end."
