#!/bin/bash
# Apply the local webrtc-c platform patches on top of the recorded submodule SHA.
#
# Idempotent: if the submodule HEAD already has the patches applied (same number
# of commits ahead of the recorded SHA as the number of patch files), this is a
# no-op. Otherwise the submodule is reset to the recorded SHA and the patches
# in patches/*.patch are applied via `git am`.
#
# Run this once after `git submodule update --init --recursive`. The first
# `idf.py build` after a fresh clone needs the patches to be present.

set -e

REPO_ROOT="$(git rev-parse --show-toplevel)"
SUBMOD="$REPO_ROOT/amazon-kinesis-video-streams-webrtc-sdk-c"
PATCHES_DIR="$REPO_ROOT/patches"

if [ ! -d "$SUBMOD/.git" ] && [ ! -f "$SUBMOD/.git" ]; then
    echo "error: webrtc-c submodule not initialized. Run:"
    echo "  git submodule update --init --recursive"
    exit 1
fi

# How many patches do we maintain?
PATCH_COUNT=$(ls "$PATCHES_DIR"/[0-9]*.patch 2>/dev/null | wc -l | tr -d ' ')
if [ "$PATCH_COUNT" -eq 0 ]; then
    echo "no patches in $PATCHES_DIR — nothing to apply"
    exit 0
fi

# Parent-recorded submodule SHA
RECORDED_SHA=$(cd "$REPO_ROOT" && git ls-tree HEAD amazon-kinesis-video-streams-webrtc-sdk-c | awk '{print $3}')
CUR_SHA=$(cd "$SUBMOD" && git rev-parse HEAD)

# How many commits is the submodule working tree ahead of the recorded SHA?
AHEAD=$(cd "$SUBMOD" && git rev-list --count "$RECORDED_SHA..HEAD" 2>/dev/null || echo 0)

if [ "$AHEAD" = "$PATCH_COUNT" ]; then
    echo "patches already applied ($AHEAD on top of $RECORDED_SHA) — no-op"
    exit 0
fi

echo "applying $PATCH_COUNT patches on top of $RECORDED_SHA in webrtc-c submodule..."
cd "$SUBMOD"
git reset --hard "$RECORDED_SHA"
git am "$PATCHES_DIR"/[0-9]*.patch
echo "done. submodule HEAD now $(git rev-parse --short HEAD) ($PATCH_COUNT patches above $RECORDED_SHA)."
