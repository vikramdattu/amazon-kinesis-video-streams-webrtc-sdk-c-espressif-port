#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Verify the integration test output. Two independent verification paths:
#
# 1. C viewer: log-grep over out/master.log + out/viewer.log for
#    signaling/ICE/DTLS markers.
# 2. Python viewer: ffprobe out/python_viewer.mkv — proves H.264 frames
#    actually arrived and were decoded into a real container file.
#
# `MODE` (env, set by run_test.sh) selects which path(s) to apply.
# Defaults to `auto` — runs whichever logs/files are present.

set -uo pipefail

cd "$(dirname "$0")"

PASS=0
FAIL=0

ok() { echo "  PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

ML=out/master.log
VL=out/viewer.log
PVL=out/python_viewer.log
PMKV=out/python_viewer.mkv
MIN_VIDEO_FRAMES="${MIN_VIDEO_FRAMES:-30}"

[ -s "$ML" ] || { echo "Error: $ML missing or empty"; exit 2; }

MODE="${MODE:-auto}"

run_c_checks=0
run_python_checks=0
case "$MODE" in
    c)      run_c_checks=1 ;;
    python) run_python_checks=1 ;;
    all)    run_c_checks=1; run_python_checks=1 ;;
    auto)
        [ -s "$VL" ]   && run_c_checks=1
        [ -s "$PVL" ]  && run_python_checks=1
        ;;
    *) echo "Unknown MODE=$MODE"; exit 2 ;;
esac

if [ "$run_c_checks" = 1 ]; then
    [ -s "$VL" ] || { echo "Error: $VL missing or empty (C-mode requested)"; exit 2; }
fi
if [ "$run_python_checks" = 1 ]; then
    [ -s "$PVL" ] || { echo "Error: $PVL missing or empty (python mode requested)"; exit 2; }
fi

echo "=== Master log checks ==="
grep -q "KVS WebRTC initialization completed successfully" "$ML" && ok "master initialized" || bad "master never initialized"
grep -q "Channel.*set up done" "$ML"                            && ok "master signaling channel ready" || bad "master signaling channel not ready"
grep -qE "(SDP_OFFER|received offer)" "$ML"                     && ok "master received offer from viewer" || bad "master never received offer"

if grep -qE "writeFrame.*0x[0-9a-f]+[1-9]" "$ML"; then
    bad "master writeFrame errors observed"
else
    ok "master had no writeFrame errors"
fi

if [ "$run_c_checks" = 1 ]; then
    echo
    echo "=== C viewer checks ==="
    grep -q "KVS WebRTC initialization completed successfully" "$VL" && ok "C viewer initialized" || bad "C viewer never initialized"
    grep -q "Signaling client connection established" "$VL"          && ok "C viewer signaling connected" || bad "C viewer signaling not connected"
    grep -qE "(ICE.*connected|Selected.*candidate|ConnectivityChecks)" "$VL" && ok "C viewer ICE connectivity reached" || bad "C viewer ICE never connected"
    if grep -qE "(Frame received|SRTP.*ready|DTLS.*completed)" "$VL"; then
        ok "C viewer received media or finished DTLS"
    else
        bad "C viewer saw no media / DTLS handshake never completed"
    fi
fi

if [ "$run_python_checks" = 1 ]; then
    echo
    echo "=== Python viewer checks ==="
    grep -q "Got [0-9]\+ TURN server" "$PVL" && ok "python viewer fetched TURN servers" || bad "python viewer never got TURN servers"
    grep -q "Sent SDP_OFFER" "$PVL"          && ok "python viewer sent SDP offer"        || bad "python viewer never sent SDP offer"
    grep -q "Applied SDP_ANSWER" "$PVL"      && ok "python viewer received SDP answer"   || bad "python viewer never got SDP answer"
    grep -q "Track received: kind=video" "$PVL" && ok "python viewer received video track" || bad "python viewer never received video track"
    grep -q "python_viewer: PASS" "$PVL"     && ok "python viewer self-reported PASS"    || bad "python viewer did not self-report PASS"

    echo "--- ffprobe $PMKV ---"
    if [ ! -s "$PMKV" ]; then
        bad "$PMKV missing or empty"
    elif ! command -v ffprobe >/dev/null 2>&1; then
        echo "  WARN: ffprobe not installed locally — skipping deeper MKV checks."
    else
        FFPROBE_JSON=$(ffprobe -v error -print_format json -show_streams -count_packets "$PMKV" 2>/dev/null || true)
        if [ -z "$FFPROBE_JSON" ]; then
            bad "ffprobe failed on $PMKV"
        else
            CODEC=$(echo "$FFPROBE_JSON" | grep -oE '"codec_name"\s*:\s*"[^"]+"' | head -1 | grep -oE '"[^"]+"$' | tr -d '"')
            NB_PACKETS=$(echo "$FFPROBE_JSON" | grep -oE '"nb_read_packets"\s*:\s*"[0-9]+"' | head -1 | grep -oE '[0-9]+' || echo 0)
            echo "  ffprobe: codec=${CODEC:-unknown} packets=${NB_PACKETS:-0}"
            [ "${CODEC:-}" = "h264" ] && ok "MKV codec is h264" || bad "MKV codec is '${CODEC:-unknown}', expected h264"
            if [ "${NB_PACKETS:-0}" -ge "$MIN_VIDEO_FRAMES" ]; then
                ok "MKV has ≥$MIN_VIDEO_FRAMES video packets ($NB_PACKETS)"
            else
                bad "MKV has only $NB_PACKETS video packets (need ≥$MIN_VIDEO_FRAMES)"
            fi
        fi
    fi
fi

echo
echo "=== Summary ==="
echo "  Mode:   $MODE"
echo "  Passed: $PASS"
echo "  Failed: $FAIL"

if [ "$FAIL" -gt 0 ]; then
    echo
    echo "Hint: re-run with AWS_KVS_LOG_LEVEL=2 for more detail."
    [ -s "$ML" ]  && { echo "      master tail:" ; tail -20 "$ML" | sed 's/^/        /'; }
    [ -s "$VL" ]  && { echo "      C viewer tail:" ; tail -20 "$VL" | sed 's/^/        /'; }
    [ -s "$PVL" ] && { echo "      python viewer tail:" ; tail -20 "$PVL" | sed 's/^/        /'; }
    exit 1
fi
echo "OK — integration test passed."
