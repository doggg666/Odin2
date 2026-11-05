#!/usr/bin/env python3
"""Local voice bridge for Odin. PCM is always mono s16le at 16 kHz."""
from __future__ import annotations
import argparse
import io
from pathlib import Path
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import shutil
import subprocess
import threading

RATE = 16000
MAX_RECORD_BYTES = RATE * 2 * 12
MAX_TEXT = 6000
MAX_PLAY_BYTES = 3 * 1024 * 1024 - 2
operation_lock = threading.Lock()
model = None
tts_voice = None
tts_engine = "piper"
tts_model_name = ""


def synthesize(text: str) -> bytes:
    if not isinstance(text, str) or not text.strip() or len(text) > MAX_TEXT:
        raise ValueError("text must contain 1-6000 characters")
    if tts_engine == "piper":
        if tts_voice is None:
            raise RuntimeError("Piper voice is not loaded")
        stream = io.BytesIO()
        with wave.open(stream, "wb") as wav:
            tts_voice.synthesize_wav(text, wav)
        speech = stream.getvalue()
    else:
        speech = subprocess.run(
            ["espeak-ng", "--stdout", "--stdin", "-v", os.getenv("ODIN_TTS_VOICE", "en-us"), "-s", "165"],
            input=text.encode("utf-8"), capture_output=True, check=True, timeout=30,
        ).stdout
    pcm = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", "pipe:0", "-t", "90", "-f", "s16le", "-ar", str(RATE), "-ac", "1", "pipe:1"],
        input=speech, capture_output=True, check=True, timeout=30,
    ).stdout
    if not pcm or len(pcm) > MAX_PLAY_BYTES:
        raise ValueError("speech exceeds playback limit")
    return pcm


def transcribe(pcm: bytes) -> str:
    if len(pcm) < RATE // 2 or len(pcm) > MAX_RECORD_BYTES or len(pcm) % 2:
        raise ValueError("expected 0.25-12 seconds of mono 16 kHz signed 16-bit little-endian PCM")
    if model is None:
        raise RuntimeError("speech recognition is not ready")
    import numpy as np
    samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
    if np.sqrt(np.mean(samples * samples)) < 0.001:
        return ""
    segments, _ = model.transcribe(samples, beam_size=1, language="en", condition_on_previous_text=False)
    return " ".join(segment.text.strip() for segment in segments).strip()


class Handler(BaseHTTPRequestHandler):
    timeout = 15

    def respond(self, code: int, data: bytes, content_type: str = "application/json") -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def error(self, code: int, message: str) -> None:
        self.respond(code, json.dumps({"error": message}).encode())

    def do_GET(self) -> None:
        if self.path != "/health":
            self.error(404, "unknown endpoint")
            return
        self.respond(200, json.dumps({"status": "ok", "stt_ready": model is not None,
                                     "tts_ready": bool(shutil.which("ffmpeg") and (tts_voice is not None if tts_engine == "piper" else shutil.which("espeak-ng"))),
                                     "tts_engine": tts_engine, "tts_voice": tts_model_name,
                                     "sample_rate": RATE}).encode())

    def do_POST(self) -> None:
        if self.path not in ("/tts", "/stt"):
            self.error(404, "unknown endpoint")
            return
        if not operation_lock.acquire(blocking=False):
            self.error(503, "voice service busy")
            return
        try:
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                raise ValueError("invalid content length")
            maximum = MAX_RECORD_BYTES if self.path == "/stt" else 32768
            if not 0 < length <= maximum:
                self.error(413, "invalid request size")
                return
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError("incomplete request body")
            if self.path == "/stt":
                text = transcribe(body)
                self.respond(200, json.dumps({"text": text}).encode())
            else:
                request = json.loads(body)
                if not isinstance(request, dict):
                    raise ValueError("expected a JSON object")
                pcm = synthesize(request.get("text"))
                self.respond(200, pcm, "audio/pcm;rate=16000;channels=1;format=s16le")
        except (ValueError, UnicodeError) as exc:
            self.error(400, str(exc))
        except Exception as exc:
            # Do not log microphone contents or prompts.
            print(f"Voice operation failed: {type(exc).__name__}", flush=True)
            self.error(502, "voice processing failed; check service dependencies")
        finally:
            operation_lock.release()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8001)
    parser.add_argument("--model", default="tiny.en")
    parser.add_argument("--tts-only", action="store_true")
    parser.add_argument("--tts-engine", choices=("piper", "espeak"), default="piper")
    parser.add_argument("--voice", default=str(Path(__file__).parent / "voices" / "en_US-ryan-high.onnx"))
    args = parser.parse_args()
    if not shutil.which("ffmpeg"):
        parser.error("Install ffmpeg first")
    global model, tts_voice, tts_engine, tts_model_name
    tts_engine = args.tts_engine
    if tts_engine == "piper":
        if not Path(args.voice).is_file():
            parser.error("Download the voice first: .voice-venv/bin/python -m piper.download_voices --data-dir voices en_US-ryan-high")
        from piper import PiperVoice
        print(f"Loading neural voice {Path(args.voice).stem}...", flush=True)
        tts_voice = PiperVoice.load(args.voice)
        tts_model_name = Path(args.voice).stem
    else:
        if not shutil.which("espeak-ng"):
            parser.error("Install espeak-ng to use the espeak engine")
        tts_model_name = os.getenv("ODIN_TTS_VOICE", "en-us")
    if not args.tts_only:
        from faster_whisper import WhisperModel
        print(f"Loading speech recognition model {args.model}...", flush=True)
        model = WhisperModel(args.model, device="cpu", compute_type="int8", cpu_threads=4)
    print(f"Odin voice bridge listening on {args.host}:{args.port}", flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
