# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# aiortc-based viewer that connects to KVS as a VIEWER role peer, offers
# recvonly H.264 + Opus, and dumps received frames to /out/python_viewer.mkv
# via aiortc's MediaRecorder (which uses PyAV / ffmpeg under the hood).
#
# Independent verification path — complements the C kvsWebrtcClientViewer
# image. Lighter to build (~1 min vs ~10 min for the C samples), and the
# saved MKV lets verify.sh run real ffprobe checks instead of log-grep.

FROM python:3.11-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        ffmpeg \
        libopus0 \
        libvpx7 \
        libsrtp2-1 \
        libssl3 \
        bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY tests/docker/python_viewer/requirements.txt /app/
RUN pip install --no-cache-dir -r requirements.txt

COPY tests/docker/python_viewer/viewer.py /app/

ENTRYPOINT ["python", "/app/viewer.py"]
