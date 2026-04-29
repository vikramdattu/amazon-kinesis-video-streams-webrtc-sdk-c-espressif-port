#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""
aiortc-based KVS WebRTC viewer.

Acts as the VIEWER side of a Kinesis Video Streams signaling channel:
- Looks up (or creates) the channel ARN.
- Resolves the WSS + HTTPS endpoints for the VIEWER role.
- Pulls TURN credentials via the signaling HTTPS endpoint.
- SigV4-presigns the WSS URL and opens the WebSocket.
- Builds an aiortc RTCPeerConnection with recvonly H.264 / Opus.
- Sends an SDP_OFFER (non-trickle ICE, like upstream's C viewer).
- Waits for SDP_ANSWER + remote ICE candidates.
- Records every received track to /out/python_viewer.mkv via MediaRecorder.
- Exits cleanly after TEST_DURATION_SEC.

Why this exists: the upstream C viewer doesn't dump received H.264 to
disk, so verify.sh has to log-grep. This Python viewer writes a real MKV
that ffprobe can validate (codec=h264, frame count, no decode errors).
"""

import asyncio
import base64
import json
import logging
import os
import sys
import time
import urllib.parse
import uuid

import boto3
from botocore.auth import SigV4QueryAuth
from botocore.awsrequest import AWSRequest
from aiortc import (
    RTCConfiguration,
    RTCIceCandidate,
    RTCIceServer,
    RTCPeerConnection,
    RTCRtpCodecCapability,
    RTCSessionDescription,
)
from aiortc.contrib.media import MediaRecorder
from aiortc.sdp import candidate_from_sdp
import websockets

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("python_viewer")
# aiortc and its sub-loggers are noisy at INFO — keep them at WARNING.
for noisy in ("aiortc", "aioice", "websockets"):
    logging.getLogger(noisy).setLevel(logging.WARNING)


REGION = os.environ.get("AWS_DEFAULT_REGION", "us-west-2")
CHANNEL = os.environ["KVS_CHANNEL_NAME"]
DURATION = int(os.environ.get("TEST_DURATION_SEC", "45"))
OUT_PATH = os.environ.get("OUT_PATH", "/out/python_viewer.mkv")
CLIENT_ID = os.environ.get("CLIENT_ID", f"py-viewer-{uuid.uuid4().hex[:8]}")


def get_channel_arn(kvs) -> str:
    """Look up the channel ARN; create the channel if it doesn't exist yet."""
    try:
        return kvs.describe_signaling_channel(ChannelName=CHANNEL)["ChannelInfo"]["ChannelARN"]
    except kvs.exceptions.ResourceNotFoundException:
        log.info("Channel %s not found — creating", CHANNEL)
        kvs.create_signaling_channel(ChannelName=CHANNEL)
        return kvs.describe_signaling_channel(ChannelName=CHANNEL)["ChannelInfo"]["ChannelARN"]


def resolve_endpoints(kvs, arn: str) -> dict:
    resp = kvs.get_signaling_channel_endpoint(
        ChannelARN=arn,
        SingleMasterChannelEndpointConfiguration={
            "Protocols": ["WSS", "HTTPS"],
            "Role": "VIEWER",
        },
    )
    return {e["Protocol"]: e["ResourceEndpoint"] for e in resp["ResourceEndpointList"]}


def build_ice_servers(https_endpoint: str, arn: str) -> list[RTCIceServer]:
    signaling = boto3.client(
        "kinesis-video-signaling", region_name=REGION, endpoint_url=https_endpoint
    )
    resp = signaling.get_ice_server_config(ChannelARN=arn, ClientId=CLIENT_ID)
    servers = [RTCIceServer(urls=[f"stun:stun.kinesisvideo.{REGION}.amazonaws.com:443"])]
    for s in resp["IceServerList"]:
        servers.append(
            RTCIceServer(urls=s["Uris"], username=s["Username"], credential=s["Password"])
        )
    log.info("Got %d TURN server(s) from KVS", len(resp["IceServerList"]))
    return servers


def sigv4_presign_wss(wss_endpoint: str, arn: str) -> str:
    """SigV4 query-string presign on the WSS URL for the kinesisvideo service."""
    creds = boto3.Session().get_credentials().get_frozen_credentials()
    params = {"X-Amz-ChannelARN": arn, "X-Amz-ClientId": CLIENT_ID}
    url = wss_endpoint + "?" + urllib.parse.urlencode(params)
    request = AWSRequest(method="GET", url=url)
    SigV4QueryAuth(creds, "kinesisvideo", REGION).add_auth(request)
    return request.url


async def run() -> int:
    log.info("region=%s channel=%s client_id=%s duration=%ds out=%s",
             REGION, CHANNEL, CLIENT_ID, DURATION, OUT_PATH)

    kvs = boto3.client("kinesisvideo", region_name=REGION)
    arn = get_channel_arn(kvs)
    endpoints = resolve_endpoints(kvs, arn)
    ice_servers = build_ice_servers(endpoints["HTTPS"], arn)
    signed_wss = sigv4_presign_wss(endpoints["WSS"], arn)

    pc = RTCPeerConnection(RTCConfiguration(iceServers=ice_servers))
    recorder = MediaRecorder(OUT_PATH)

    # Receive-only H.264 + Opus, matching what the C master sends.
    # KVS master is configured for H264 video and OPUS audio only;
    # if the offer advertises VP8/VP9/G.711 first, the master rejects
    # the offer entirely (kvs_webrtc Failed to process offer 0xc).
    # Pin codec preferences to the codecs we actually want.
    video_caps = [
        RTCRtpCodecCapability(mimeType="video/H264", clockRate=90000,
                              parameters={
                                  "level-asymmetry-allowed": "1",
                                  "packetization-mode": "1",
                                  "profile-level-id": "42e01f",
                              }),
    ]
    audio_caps = [
        RTCRtpCodecCapability(mimeType="audio/opus", clockRate=48000, channels=2),
    ]
    video_t = pc.addTransceiver("video", direction="recvonly")
    video_t.setCodecPreferences(video_caps)
    audio_t = pc.addTransceiver("audio", direction="recvonly")
    audio_t.setCodecPreferences(audio_caps)

    track_count = 0

    @pc.on("track")
    def on_track(track):
        nonlocal track_count
        track_count += 1
        log.info("Track received: kind=%s id=%s", track.kind, track.id)
        recorder.addTrack(track)

    @pc.on("connectionstatechange")
    async def on_state():
        log.info("PC connection state: %s", pc.connectionState)

    @pc.on("iceconnectionstatechange")
    async def on_ice_state():
        log.info("ICE connection state: %s", pc.iceConnectionState)

    # Generate offer + wait for non-trickle ICE gathering to complete.
    await pc.setLocalDescription(await pc.createOffer())
    while pc.iceGatheringState != "complete":
        await asyncio.sleep(0.05)
    log.info("ICE gathering complete; opening WSS to %s", endpoints["WSS"])

    async with websockets.connect(signed_wss, max_size=2**20) as ws:
        offer_payload = {"type": pc.localDescription.type, "sdp": pc.localDescription.sdp}
        await ws.send(json.dumps({
            "action": "SDP_OFFER",
            "messagePayload": base64.b64encode(json.dumps(offer_payload).encode()).decode(),
        }))
        log.info("Sent SDP_OFFER to master")

        await recorder.start()
        deadline = time.monotonic() + DURATION

        async def pump_signaling():
            async for raw in ws:
                # KVS occasionally sends keepalive frames or non-JSON
                # control frames; skip anything that doesn't parse as
                # an object with a messageType.
                try:
                    msg = json.loads(raw)
                except (json.JSONDecodeError, TypeError):
                    log.debug("Non-JSON ws frame, len=%d", len(raw) if raw else 0)
                    continue
                if not isinstance(msg, dict):
                    continue
                mtype = msg.get("messageType", "").upper()
                payload_b64 = msg.get("messagePayload", "")
                if not payload_b64:
                    continue
                try:
                    payload = base64.b64decode(payload_b64).decode()
                except Exception:
                    log.warning("Could not decode payload for %s", mtype)
                    continue

                if mtype == "SDP_ANSWER":
                    answer = json.loads(payload)
                    await pc.setRemoteDescription(
                        RTCSessionDescription(sdp=answer["sdp"], type=answer["type"])
                    )
                    log.info("Applied SDP_ANSWER (sdp=%d bytes)", len(answer["sdp"]))
                elif mtype == "ICE_CANDIDATE":
                    cand_obj = json.loads(payload)
                    cand_str = cand_obj.get("candidate", "")
                    if not cand_str:
                        continue
                    # aiortc's helper parses the "candidate:..." line.
                    candidate = candidate_from_sdp(cand_str.split(":", 1)[1])
                    candidate.sdpMid = cand_obj.get("sdpMid")
                    candidate.sdpMLineIndex = cand_obj.get("sdpMLineIndex")
                    await pc.addIceCandidate(candidate)
                    log.info("Added remote ICE candidate")
                elif mtype == "STATUS_RESPONSE":
                    code = msg.get("statusCode")
                    if code and str(code) != "200":
                        log.warning("STATUS_RESPONSE %s: %s", code, msg)
                else:
                    log.debug("Ignored signaling message type=%r", mtype)

        signaling_task = asyncio.create_task(pump_signaling())
        try:
            await asyncio.wait_for(asyncio.shield(signaling_task),
                                   timeout=max(1.0, deadline - time.monotonic()))
        except asyncio.TimeoutError:
            log.info("Reached test duration (%ds), stopping", DURATION)
        finally:
            signaling_task.cancel()

    await recorder.stop()
    await pc.close()

    log.info("Recording closed. Tracks observed: %d. Output: %s", track_count, OUT_PATH)
    if track_count == 0:
        log.error("No tracks ever arrived — viewer failed")
        return 1
    if not os.path.exists(OUT_PATH) or os.path.getsize(OUT_PATH) < 1024:
        log.error("Output file missing or implausibly small")
        return 2
    log.info("python_viewer: PASS")
    return 0


if __name__ == "__main__":
    try:
        rc = asyncio.run(run())
    except KeyboardInterrupt:
        rc = 130
    sys.exit(rc)
