# Odin microphone and spoken replies

The panel uses two servers on the local network:

- LM Studio: `http://192.168.1.93:1234/v1` for chat.
- This computer: `http://192.168.1.238:8001` for speech recognition and synthesis.

The voice bridge is a separate Python service. It uses faster-whisper tiny.en
on CPU for English transcription and Piper with the Ryan high-quality neural
voice for speech synthesis. FFmpeg converts its output to the board's 16 kHz PCM format. The first launch downloads the recognition model. Piper provides a more natural voice than the original eSpeak output. Recognition can mishear names; typed input still works.

From this directory, restart the service with:

```bash
./start-voice.sh
```

To create its isolated environment on another computer (Python 3.10+):

```bash
python3 -m venv .voice-venv
.voice-venv/bin/python -m pip install -r voice-requirements.txt
./start-voice.sh
```

Install `ffmpeg` through that computer's package manager first, then download
the default voice:

```bash
.voice-venv/bin/python -m piper.download_voices --data-dir voices en_US-ryan-high
```

The voice model card is retained in `voices/ryan-high-MODEL_CARD.md`; it lists
the RyanSpeech dataset and its CC BY-NC-SA 4.0 license.
Piper API reference: https://github.com/OHF-Voice/piper1-gpl/blob/main/docs/API_PYTHON.md

Use `./start-voice.sh --voice /path/to/another.onnx` to choose another Piper
voice. `--tts-engine espeak` explicitly selects the previous engine (requires
espeak-ng). The health endpoint reports the active engine and voice.

A sample of the new voice is in `samples/odin-piper-preview.wav`.


For improved transcription, run `./start-voice.sh --model base.en`. Only one
voice bridge should run on port 8001 at a time. The running service is not
configured to start automatically after a computer reboot. If its LAN address
changes, update `firmware/main/odin_voice.h`, rebuild, and flash.

## Panel controls

Open **Talk to Odin**. **Hold to talk** records while held, up to 12 seconds;
release to transcribe, ask LM Studio, and play its reply. **Speak replies** also
enables TTS for typed prompts. Uncheck it for text-only responses.
**Hold: mic test** records while held and plays the recording back locally,
without Wi-Fi or server access. Hold for at least a quarter second.
The amplifier stays muted outside playback, and playback starts at half volume.
All recording, network work, and playback run outside the LVGL UI task.

## Transport

`GET /health` reports readiness. `POST /stt` accepts raw little-endian signed
16-bit mono PCM at 16000 Hz (0.25-12 seconds), returning `{"text":"..."}`.
`POST /tts` accepts `{"text":"..."}` and returns the same PCM format. Responses
are capped at 90 seconds / less than 3 MB. The board holds bounded buffers in
PSRAM. Voice requests have a 90-second network timeout. Audio is kept in memory
and is not written to recordings or logged. The bridge is intended for the local
LAN and has no authentication configured.

## Hardware provenance

V1.0 settings follow Elecrow's official
`example/V1.0/idf-code/Lesson11-Playback_After_Recording` at repository commit
`32fe6132f95835d4872d7aa673c685008ec4c37a`:

- PDM microphone: I2S0, clock GPIO24, data GPIO26, 16 kHz mono PCM.
- Speaker: I2S1, BCLK GPIO22, LRCLK GPIO21, data GPIO23, stereo slots.
- Amplifier enable: GPIO30, active low.

Source: https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/tree/master/example/V1.0/idf-code/Lesson11-Playback_After_Recording

## Validation

Firmware built and was flashed with ESP-IDF 5.5. Board logs confirm 16 kHz
PDM/I2S initialization, saved Wi-Fi reconnect, and LM Studio discovery. The live voice bridge synthesized and
transcribed “The microphone and speaker test is working.” successfully. Invalid
text, JSON shape, and PCM requests returned HTTP 400. Hardware audio capture,
physical speaker output, and the complete panel voice conversation require a
user to speak and listen; use the mic test before testing the full pipeline.

Piper validation: model loaded successfully; synthesized a sample and transcribed
it through the existing recognizer. Output remains mono s16le at 16 kHz, so the
board firmware needs no changes.
