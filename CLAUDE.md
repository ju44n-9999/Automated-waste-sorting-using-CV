# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## System overview

Two ESP32-S3 boards that talk over ESP-NOW (no router needed between them):

1. **Camera / classifier** — `waste_classifier_verllm/firmware/` (Arduino sketch built with PlatformIO). Captures a JPEG, classifies it as plastic / metal / paper, and broadcasts the label over ESP-NOW. Two classification paths:
   - On-device TFLite model (`model_data.cpp`, INT8 128×128×3 → 3 logits, 4 MB arena in PSRAM).
   - Groq vision LLM called directly from the board over HTTPS (`llm_config.h`). Rate limits / cooldown (`LLM_CAPTURE_INTERVAL_MS`, 429 `Retry-After`) fall back to the local model.
   - Physical capture button on GPIO 14 (active LOW, internal pull-up) and a WS2812B status LED on GPIO 3, driven by its own FreeRTOS task (`ledTask`): slow red blink = booting, steady red = no station Wi-Fi (hotspot-only counts), yellow = analyzing, green = ready, fast red blink = failed analysis or camera/model fault. All capture paths go through `classifyWithLed()`. Camera uses GPIO 4–13, 15–18 (4/5 = SCCB), so don't put peripherals there.
   - Also serves a web UI (`web_page.h`) with `/classify`, snapshot and pause/resume endpoints. With empty `WIFI_SSID` in `wifi_config.h` it runs its own `WasteCamera` hotspot on channel 6.
2. **Sorting controller** — `devicesControl/` (PlatformIO). Receives tags, drives two facing servos to a per-material angle, holds, returns home.

`waste_classifier_verllm/backend/` is a separate FastAPI service (`/health`, `POST /classify`) offering the same classification via Groq or OpenAI with a persisted quota (`quota.py`, SQLite). The camera firmware does **not** currently call it; it talks to Groq directly. Backend config is env-driven (`MOCK_MODE` defaults to `true`, `LLM_PROVIDER`, `GROQ_MODEL`, `DEVICE_TOKEN`, `GROQ_MAX_CALLS_24H`, …; see `Settings.from_env` in `app.py`).

## Environment setup

Python deps (backend, tests, pyserial) are in the conda env defined by `environment.yml` at the repo root:

```bash
conda env create -f environment.yml      # first time
conda env update -f environment.yml --prune   # after editing environment.yml
conda activate waste-sorting
```

Use `conda run -n waste-sorting <cmd>` for non-interactive shells. Firmware builds do not use this env.

## Commands

PlatformIO is at `~/.platformio/penv/bin/pio` (not on PATH).

```bash
# Sorting controller
cd devicesControl
~/.platformio/penv/bin/pio run                 # build
~/.platformio/penv/bin/pio run -t upload       # flash (port /dev/ttyACM0)
~/.platformio/penv/bin/pio device monitor      # 115200 baud

# Controller unit tests: plain host C++ with its own main(), not PlatformIO Unity
g++ -std=c++17 -Iinclude test/test_waste_classifier.cpp lib/wasteClassifier/wasteClassifier.cpp -o /tmp/t && /tmp/t

# Camera firmware (16 MB flash, OPI PSRAM, pioarduino Arduino core 3.x)
cd waste_classifier_verllm/firmware && ~/.platformio/penv/bin/pio run -t upload

# Backend tests (run from waste_classifier_verllm/ inside the waste-sorting env)
cd waste_classifier_verllm && python -m pytest tests -q
python -m pytest tests/test_groq.py::test_groq_429_backoff_persisted -q   # single test
```

Upload fails with "port is busy" when a serial monitor (e.g. the VS Code PlatformIO one) has `/dev/ttyACM0` open — close it first.

## Sorting controller architecture (`devicesControl/`)

- `src/main.cpp` only calls `servoControlInit()`; all work runs in FreeRTOS tasks. Only `main.cpp` belongs in `src/`.
- Each module is its own folder under `lib/` holding its `.h` and `.cpp`: `servoControl` (servo motion + pin definitions), `espNowComms` (receive callback → `wasteQueue`), `wasteClassifier` (pure tag→angle logic, no Arduino dependency so it builds on the host for tests).
- Shared tunables live in `include/config.h`. `platformio.ini` has `build_flags = -I include` because otherwise `lib/` modules resolve `"config.h"` to an ESP-IDF header of the same name.
- Data flow: ESP-NOW callback (WiFi task context — use `xQueueSend(..., 0)`, never the `FromISR` variants) → `wasteQueue` → `servoListenTask` (core 1) → `sortWasteByTag()`. The same task also accepts tags typed into USB serial (one per line), which is the way to test without the camera.
- Servo 2 faces servo 1, so it is always driven to `180 - angle` (`calculateMirroredAngle`).
- Hardware: servos on GPIO 4 and 5 need their own 5–6 V supply with common ground; powering them from the board causes `BROWNOUT_RST` boot loops.
- Angles assume positional 180° servos. `SERVO_IS_CONTINUOUS_360` in `config.h` is not read by any code yet; on 360° (continuous) servos `write()` sets speed (90 = stop), so the angle logic does not work.

## Known integration gaps (camera → controller)

These are unresolved; check before assuming end-to-end works:
- **Packet format mismatch.** Camera sends packed `WastePacket {char magic[6]="WASTE"; uint32 capture_id; float score; uint32 inference_ms; char label[16]}` (34 bytes). Controller expects `WasteSortPacket {char tag[16]; uint32 messageId}` (20 bytes); on size mismatch it treats the bytes as a raw string, so the tag becomes `"WASTE"`.
- **Label mismatch.** Camera labels are `plastic`, `metal`, `paper_cardboard`; controller accepts `METAL`, `PLASTIC(S)`, `PAPER` (case-insensitive). `paper_cardboard` is rejected.
- **Channel.** Camera stays on Wi-Fi channel 6; controller's `ESPNOW_WIFI_CHANNEL` (1) is defined but never applied.
