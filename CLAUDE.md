# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## System overview

Two ESP32-S3 boards that talk over ESP-NOW (no router needed between them):

1. **Camera / classifier** — `waste_classifier_verllm/firmware/` (Arduino sketch built with PlatformIO). Captures a JPEG, classifies it as plastic / metal / paper, and broadcasts the label over ESP-NOW. Two classification paths:
   - On-device TFLite model (`model_data.cpp`, INT8 128×128×3 → 3 logits, 4 MB arena in PSRAM).
   - Groq vision LLM called directly from the board over HTTPS (`llm_config.h`). Rate limits / cooldown (`LLM_CAPTURE_INTERVAL_MS`, 429 `Retry-After`) fall back to the local model.
   - Then Gemini (`gemini`, `gemini2`, `gemini3`: one provider per key, keys in the git-ignored `llm_secrets.h`). Providers sharing a `group` are rotated round-robin on every capture. Only a 429 moves on to the next key of the same group. A timeout or 5xx skips the rest of that group and goes to the local model.
   - Also serves a web UI (`web_page.h`) with `/classify`, snapshot and pause/resume endpoints. It always runs its own `WasteCamera` hotspot on the fixed `WIFI_CHANNEL` (`wifi_config.h`, default 6).
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

- `src/main.cpp` calls `servoControlInit()` in `setup()` and `servoControlLoop()` in `loop()`. Only `main.cpp` belongs in `src/`.
- Each module is its own folder under `lib/` holding its `.h` and `.cpp`: `servoControl` (servo motion + pin definitions), `espNowComms` (receive callback → `wasteQueue`), `wasteClassifier` (pure tag→angle logic, no Arduino dependency so it builds on the host for tests).
- Shared tunables live in `include/config.h`. `platformio.ini` has `build_flags = -I include` because otherwise `lib/` modules resolve `"config.h"` to an ESP-IDF header of the same name.
- Data flow: ESP-NOW callback (WiFi task context — use `xQueueSend(..., 0)`, never the `FromISR` variants) → `wasteQueue` → `servoControlLoop()` → `sortWasteByTag()` (move, hold `SERVO_HOLD_MS`, return home). The loop also accepts tags / angles / `HOME` / `SWEEP` / `SCAN` typed into USB serial, which is the way to test without the camera.
- Single servo on `SERVO_PIN` (GPIO 4). `calculateMirroredAngle` and `test/test_servo_mirror.cpp` are left over from the old two-servo design; that test no longer compiles (`SERVO_STEP_DEG` is gone).
- Hardware: the servo needs its own 5–6 V supply with common ground; powering it from the board causes `BROWNOUT_RST` boot loops.
- Angles assume positional 180° servos. `SERVO_IS_CONTINUOUS_360` in `config.h` is not read by any code yet; on 360° (continuous) servos `write()` sets speed (90 = stop), so the angle logic does not work.

## Camera → controller link

- Camera broadcasts packed `WastePacket {char magic[6]="WASTE"; uint32 capture_id; float score; uint32 inference_ms; char label[16]}` (34 bytes) from its AP interface. Controller parses it as `CameraWastePacket` (also still accepts the legacy 20-byte `WasteSortPacket` and raw strings).
- Labels `plastic`, `metal`, `paper_cardboard` map to the angles in `config.h` (`ANGLE_*`), which must match `tags.txt` (PAPER 20, PLASTIC 45, METAL 75).
- Channel: the camera is pinned to `WIFI_CHANNEL` (`wifi_config.h`, default 6). If the saved router turns out to be on another channel, `startWifi()` / `enforceChannel()` drop it and fall back to hotspot-only, so it never drifts (the `WiFi.begin` channel argument alone is only a scan hint). Keep `ESPNOW_WIFI_CHANNEL` in `config.h` equal to it. The controller scans for `CAMERA_AP_SSID` at boot and tunes to it, rescans (async) after `ESPNOW_RESCAN_IDLE_MS` without packets, and on the `SCAN` serial command. `ESPNOW_WIFI_CHANNEL` is only the fallback.
- Camera side of the channel-hopping protocol in `waste_classifier_verllm/firmware/ESP_NOW_BROADCAST_GUIDE.md` (`serviceEspNow()`): it never leaves `WIFI_CHANNEL`. It beacons a `"PING"` `WastePacket` every 150 ms (1 s once a `WACK` ACK has been seen in the last 3 s) and listens for the 12-byte `WACK` `ControllerAckPacket`. Each label is resent every 150 ms for up to 3 s until a `WACK` echoes its `capture_id`, but only once some `WACK` has been seen, because a controller without ACKs would sort duplicates. There is deliberately no channel sweep (pattern A): it would move the hotspot and lock the controller onto the wrong channel.
- `devicesControl` builds against Arduino core 3.x, so the ESP-NOW receive callback takes `const esp_now_recv_info_t *`.
