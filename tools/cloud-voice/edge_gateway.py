#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Experimental Edge TTS -> HTTPS/JSON PCM companion; not an RTOS-native API.

No recording, no LLM and no other provider keys. Nothing is sent until an
authenticated POST. No audio files, transcripts or bearer tokens are logged.
Requires explicitly installed edge-tts and ffmpeg; no runtime pip/download.
"""
import argparse
import asyncio
import base64
import hmac
import json
import os
import re
import shutil
import ssl
import subprocess
from http.server import BaseHTTPRequestHandler, HTTPServer

MAX_PCM = 320000
VOICE = re.compile(r"[a-zA-Z0-9-]{3,96}\Z")


def validate(payload):
    if not isinstance(payload, dict) or set(payload) != {"text", "voice"}:
        raise ValueError("invalid request")
    text, voice = payload["text"], payload["voice"]
    if (not isinstance(text, str) or not text or "\0" in text
            or len(text.encode("utf-8")) > 4096
            or not isinstance(voice, str) or not VOICE.fullmatch(voice)):
        raise ValueError("invalid text/voice")
    return text, voice


async def synthesize(text, voice):
    import edge_tts  # Optional companion dependency, never imported on board.
    media = bytearray()
    async for item in edge_tts.Communicate(text, voice=voice).stream():
        if item["type"] == "audio":
            media.extend(item["data"])
            if len(media) > 4 * 1024 * 1024:
                raise ValueError("compressed audio exceeds limit")
    if not media:
        raise ValueError("no audio")
    # Decode slightly more than 10s. Overlong audio is rejected in its entirety,
    # NEVER presented as successfully played truncated PCM.
    decoded = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
         "-f", "mp3", "-i", "pipe:0", "-t", "10.001", "-vn", "-ac", "1",
         "-ar", "16000", "-f", "s16le", "pipe:1"],
        input=bytes(media), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        timeout=10, check=True)
    pcm = decoded.stdout
    if not pcm or len(pcm) > MAX_PCM or len(pcm) % 2:
        raise ValueError("empty, malformed or overlong PCM")
    return dict(sample_rate=16000, channels=1, bits=16,
                pcm=base64.b64encode(pcm).decode("ascii"))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, *unused):
        pass  # Never emit headers, request bodies, user text or URLs.

    def reply(self, status, obj):
        body = json.dumps(obj, ensure_ascii=True, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/v1/tts":
            return self.reply(404, {"error": "not found"})
        supplied = self.headers.get_all("Authorization", [])
        if len(supplied) != 1 or not hmac.compare_digest(
                supplied[0].encode(), ("Bearer " + self.server.token).encode()):
            return self.reply(401, {"error": "unauthorized"})
        lengths = self.headers.get_all("Content-Length", [])
        if (len(lengths) != 1 or len(lengths[0]) > 6 or not lengths[0].isascii()
                or not lengths[0].isdecimal() or "Transfer-Encoding" in self.headers):
            return self.reply(400, {"error": "length required"})
        n = int(lengths[0])
        if not 1 <= n <= 16384:
            return self.reply(413, {"error": "request too large"})
        try:
            data = self.rfile.read(n)
            if len(data) != n:
                raise ValueError("short request")
            text, voice = validate(json.loads(data.decode("utf-8")))
            result = asyncio.run(asyncio.wait_for(synthesize(text, voice), 40))
        except (ValueError, UnicodeError, TimeoutError, subprocess.SubprocessError):
            return self.reply(422, {"error": "synthesis incomplete; no audio published"})
        except Exception:
            return self.reply(502, {"error": "upstream unavailable; no audio published"})
        self.reply(200, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1",
                        help="explicit LAN address required to expose to A7Z")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert", required=True, help="TLS server certificate PEM")
    parser.add_argument("--key", required=True, help="TLS private key PEM")
    args = parser.parse_args()
    token = os.environ.get("AIPET_EDGE_GATEWAY_TOKEN", "")
    if len(token) < 32 or len(token) > 255 or any(ord(c) < 33 or ord(c) > 126 for c in token):
        parser.error("set AIPET_EDGE_GATEWAY_TOKEN to a random 32..255 character ASCII token")
    if not shutil.which("ffmpeg"):
        parser.error("ffmpeg must be installed explicitly")
    import edge_tts  # Fail before binding if the optional package is missing.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    server = HTTPServer((args.bind, args.port), Handler)  # bounded single owner
    server.token = token
    # Handshake happens on first I/O AFTER Handler.setup installs a timeout;
    # an unauthenticated TCP client must not hold accept() forever.
    server.socket = context.wrap_socket(server.socket, server_side=True,
                                       do_handshake_on_connect=False)
    print("Experimental Edge companion ready over HTTPS. No API call yet. Ctrl+C stops.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
