# AGENTS.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

## What this repository is

Firmware for **Clawd Mochi Plus**, an ESP32-C3 + ST7789 desk companion: 15 parametric expressions, a runtime-adjustable eye geometry engine, an idle "life" state machine, and STA-first WiFi with AP provisioning. It is a single-file Arduino sketch (`clawd_mochi/clawd_mochi.ino`, ~1500 lines including embedded HTML) plus an offline preview page and a Python hook bridge.

The user-facing reference is `README.md` (Chinese, the default) / `README_en.md` (English) (features, wiring, API tables, expression keys). Keep those in sync with behavior changes, and add entries to `CHANGELOG.md` (Keep a Changelog, `[Unreleased]` section).

## Build, flash, verify

`arduino-cli` is **not installed in this environment**; the GitHub Actions workflow is the compile gate. When it is available, use the exact board options from CI:

```bash
# one-time toolchain (versions are pinned in .github/workflows/compile.yml)
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli lib install "Adafruit GFX Library@1.12.6" "Adafruit ST7735 and ST7789 Library@1.11.0"

# compile (run from repo root; the sketch directory is the build unit)
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 clawd_mochi

# flash
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 -p COM<n> clawd_mochi
```

There are no unit tests. Verification is: compile cleanly, then exercise the device over HTTP:

```bash
curl "http://clawd-mochi.local/health"
curl "http://clawd-mochi.local/cmd?k=e&lease=1800000"
curl "http://clawd-mochi.local/state"
curl "http://192.168.4.1/health"            # first-boot AP mode
```

Test the hook bridge without hardware dependencies beyond the device:

```powershell
'{"hook_event_name":"PreToolUse"}' | python examples/clawd-notify.py
$env:CLAWD_URL="http://192.168.4.1"; '{"hook_event_name":"Stop"}' | python examples/clawd-notify.py
```

`preview.html` is an offline, frame-accurate player for eyeballing expression changes without hardware. Its frames are pre-generated PNG data URIs, so it does **not** reflect code or `/eye` geometry changes until regenerated.

Connecting the host to the device's `ClaWD-Mochi` hotspot changes the host's WiFi connection — ask the user before doing it, and restore the original network afterwards.

## Firmware architecture

Everything lives in one translation unit, organized top-to-bottom:

1. Pins, WiFi constants, eye-geometry globals and tuning `#define`s (`clawd_mochi.ino:32`).
2. Drawing helpers and views: logo, normal eyes, squish (`> <`), info/boot pages, blocking intro animations.
3. Expression engine and idle engine.
4. Embedded web UI as `PROGMEM` raw string literals (`INDEX_HTML`, `WIFI_HTML`).
5. HTTP route handlers.
6. `setup()` (display, WiFi, route registration, boot info page) and `loop()`.

### Expression engine

- `enum Expr` (`clawd_mochi.ino:89`) mixes persistent views (`EXPR_NORMAL=0`, `EXPR_SQUISH=1`) and temporary expressions (everything `>= EXPR_HAPPY`). The "temporary" test is literally `currentExpr >= EXPR_HAPPY`; new expressions must keep values at or above it. `EXPR_SQUISH` is a separate always-on view, not one of the 15.
- Every expression is drawn from the runtime geometry (`eyeW/eyeH/eyeGap/eyeOx/eyeOy`) via `eyeLX()` / `eyeRX()` / `eyeY()` / `eyeCY()` (`clawd_mochi.ino:268`). Never hardcode 240×240 coordinates — `/eye` rescales all expressions.
- Adding or changing an expression requires touching all of: the enum, a `drawExprX()` function, the `drawCurrentExpr()` switch (`clawd_mochi.ino:786`), `exprFrameCount()` (`:760`), `exprFramePeriod()` (`:773`), and the `KEY_MAP` table (`clawd_mochi.ino:1224`). Also update the README expression table and, if frames changed, `preview.html` / `docs/images/`.
- `drawCurrentExpr()` does a full `fillScreen()` per frame. Frame periods are deliberately tuned so a full-screen redraw does not starve `server.handleClient()`; do not make animated expressions faster without accounting for HTTP responsiveness.
- Temporary expressions auto-restore via `exprEndAt` (5 s; Think 6 s). `/cmd?lease=<ms>` overrides this for temporary expressions only; `normal` and `squish` ignore the lease.
- `/cmd` controls:
  - `w`/`normal` → persistent normal view; `s`/`squish` → persistent squish view.
  - `n` → silent restore to normal eyes, but only if currently in a temporary expression (it does not leave the squish view).
  - Unknown/empty keys → HTTP 400.
- `startExpr()`, `animNormalEyes()` and `animSquishEyes()` contain **blocking** `delay()` animations (surprised/dizzy shake, intro wiggle). The response is sent before the animation starts; the web UI's `waitNotBusy()` polls `/state` to know when the single-threaded `WebServer` is free again. Avoid adding blocking work to `loop()`.

### Idle engine

- `idleTick()` (`clawd_mochi.ino:884`) is a non-blocking phase machine: blink → look out / dwell / look back → squint, then nap after 5 minutes without activity. Blinks use the separate `blinkFxTick()` toggle pump.
- Idle blink/look/squint redraw only the horizontal eye band (`drawEyesBand`), not the whole screen.
- Nap runs Sleepy for `NAP_ANIM_MS` (60 s) and then sets `napStatic` to freeze redraws while leaving the backlight on. Any activity (`noteActivity()`) wakes it.
- All deadlines use wrap-safe `(int32_t)(now - deadline) >= 0` comparisons. Follow that pattern for new timers.

### Geometry and persistence

- `clampEyeParams()` (`clawd_mochi.ino:407`) is the single source of truth for valid ranges; it enforces `2*w + gap <= 236` and reserves margins for idle pan, squint overhang, tears and the think wave. Call it after any mutation.
- NVS namespaces:
  - `wifi`: `ssid`, `pass` (written by `POST /wifi`, then reboot).
  - `mochi`: `ew/eh/eg/ex/ey` (only persisted on `/eye?...&save=1`) and `idle` (persisted immediately by `/idle`).
- `/speed` scales only the blocking intro animations through `speedMs()`; expression frame periods are fixed by `exprFramePeriod()`. Do not assume it speeds up animated expressions.
- There is no build-time WiFi configuration, `wifi_config.h`, or static-IP macro. Provisioning is runtime-only; do not reintroduce compile-time credentials.

### WiFi

- STA-first: saved credentials → connect as station (15 s boot wait) → mDNS `clawd-mochi.local`. First boot or failed connect → `ClaWD-Mochi` AP (`clawd1234`, `192.168.4.1`) in AP+STA while STA keeps retrying. The AP exists only as a boot-time fallback; the 10 s watchdog only calls `WiFi.reconnect()`.
- **RF workaround — do not remove:** `WiFi.setTxPower(WIFI_POWER_8_5dBm)` is applied after *every* `WiFi.mode()` switch because it is a no-op before the radio starts. Cheap ESP32-C3 Super Mini boards fail auth/assoc (`AUTH_EXPIRE`) at default max TX power. See README and `CHANGELOG.md` (verified 2026-10-19).
- 2.4 GHz only; no static IP; mDNS failures are non-fatal.

### HTTP and embedded UI

- Routes are registered in `setup()` before the boot info page, so any early request dismisses that page (`bootRequestSeen`).
- `/state` (`clawd_mochi.ino:1374`) is the contract consumed by the embedded page and external tooling. It returns `bl, speed, idle, expr, eye{w,h,gap,ox,oy}, bg, mode, ip, hostname`, plus `rssi` in STA mode and `heap`. Append fields rather than renaming/removing existing ones.
- The device serves one request at a time; keep route handlers short and avoid full-screen redraws inside them where possible.
- The embedded UI is delivered via `PROGMEM` raw string literals (`R"rawhtml(...)rawhtml"`). Do not let the delimiter sequence appear inside the page, and keep the controller free of build steps or CDN dependencies.
- All dynamic responses are sent with `Cache-Control: no-store`.

### Preview and example

- `examples/clawd-notify.py` maps Claude Code hook events to `/cmd` keys with leases, defaults to `http://clawd-mochi.local` (`CLAWD_URL` override), and uses a 0.4 s timeout so an offline device never stalls a session. Hook event names and mappings are documented in README.
- `preview.html` embeds the 15 expressions' frames as base64 PNGs plus per-expression frame counts/periods. Regenerating it means re-rendering frames from the firmware; editing the HTML alone cannot make it match a geometry or drawing change.

## Code conventions

- 2-space indentation, `camelCase` for functions/variables, `C_*` palette constants, `#define` for pin/tuning constants, `PROGMEM` for static artwork.
- In-file comments are mixed Chinese and English; match the language of the surrounding block. User-facing docs are bilingual (`README.md` Chinese by default + `README_en.md` English).
- `loop()` must stay non-blocking (millis-based ticks); blocking `delay()` is limited to `setup()` and the explicit intro/one-shot animations.
- Do not add bitmap assets or frame buffers — the design is primitive-drawn at runtime (`Adafruit_GFX`) with no RAM framebuffer.
- Commit messages are written in Chinese (see `git log`).

## Hardware safety

- Display VCC must be **3.3 V only**; 5 V can destroy the panel.
- SPI: SCK = GPIO 8, MOSI = GPIO 10. Do not use GPIO 6/7 (SPI flash) — the board will fail to boot.
- Pins: CS 4, DC 1, RST 2, BL 3. SPI runs at 40 MHz, rotation 1.
- The repository ships no 3D-print files by design; the case comes from the upstream MakerWorld model.
