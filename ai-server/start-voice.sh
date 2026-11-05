#!/usr/bin/env bash
set -euo pipefail
odin_voice_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$odin_voice_dir"
exec .voice-venv/bin/python voice_bridge.py --host 0.0.0.0 --port 8001 "$@"
