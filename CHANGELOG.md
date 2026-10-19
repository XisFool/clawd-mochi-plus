# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- WiFi provisioning: `GET /wifi` form and `POST /wifi` (SSID/password saved to
  NVS namespace `wifi`, then reboot), mDNS `clawd-mochi.local`, and
  `GET /health` with `Cache-Control: no-store`.
- STA-first boot: saved credentials connect to the home network; first boot
  and failed connections bring up the `ClaWD-Mochi` AP hotspot in AP+STA mode
  while STA keeps retrying.
- `/cmd` `lease=<ms>` parameter (500–1800000) for temporary expressions.

### Changed

- Web controller reduced to a minimal expression page: 15 expression buttons,
  AP/STA link status, backlight toggle, and a WiFi settings link.
- `/cmd` uses an explicit key/alias table; unknown or empty keys return
  HTTP 400.
- Nap animation freezes after 60 s (`NAP_ANIM_MS`) without touching the
  backlight; activity still wakes it normally.
- `/state` adds `hostname`, drops the `term`, `view`, and `busy` fields, and
  is sent with `Cache-Control: no-store`.
- `examples/clawd-notify.py`: 0.4 s timeout and lease-aware hook mapping.
- Boot no longer overrides a command received while the info page is visible.

### Fixed

- WiFi now boots at 8.5 dBm TX power, re-applied after every `WiFi.mode()`
  switch (`setTxPower()` is a no-op before the radio starts). Cheap
  ESP32-C3 Super Mini boards emit a degraded spectrum at the default max
  power: APs cannot hear auth/assoc frames (AUTH_EXPIRE / ASSOC_EXPIRE /
  HANDSHAKE_TIMEOUT), and the provisioning AP beacon becomes unreliable or
  invisible. Verified 2026-10-19: STA and soft-AP connect and hold
  reliably at 8.5 dBm.

### Removed

- Runtime AP fallback after three failed reconnects; the 10 s watchdog now
  only calls `WiFi.reconnect()`. The AP remains a boot-time provisioning
  fallback.
- `hold` compatibility alias for `/cmd`'s `lease` parameter.
- Unreachable HTTP 409 busy response and the internal `busy` state; the web
  controller now waits for a completed `/state` request to track blocking
  animations.
- Terminal view (`/char`, keys `d`/`q`) and its `term*` drawing code.
- Drawing canvas (`/canvas`, `/draw/clear`, `/draw/stroke`).
- Runtime info-page (`i`) and logo-replay (`a`) commands; the boot logo and
  boot info page remain.
- `ENABLE_HOME_WIFI`, `WIFI_SSID`/`WIFI_PASS`, and the static IP macros.

## [1.0.0] - 2026-10-13

Initial release.

### Added

- Firmware (`clawd_mochi/clawd_mochi.ino`, single file):
  - 15 parametric expressions drawn at runtime with `Adafruit_GFX` primitives —
    normal, happy, sleepy, surprised, wink, dizzy, angry, heart, think,
    executing, crying, shocked, cool, embarrassed, delicious — plus the
    classic always-on squeeze-eyes (`> <`) view.
  - Runtime-adjustable eye geometry engine (`/eye`, NVS persistence with
    explicit `save=1`); all expressions rescale proportionally.
  - Idle life state machine: randomized blinking (2.5–6 s), left/right
    glances (6–15 s), brief squints (every 30–60 s), and a 5-minute nap
    (`zZ` + dozing bars); `/idle?on=0` disables it.
  - Zero-config dual-mode WiFi: standalone AP hotspot by default, optional
    STA with static IP via `ENABLE_HOME_WIFI`.
  - Built-in mobile-friendly web controller: expression pad, eye-geometry
    sliders, background/pen colour pickers, terminal view, drawing canvas.
  - REST API: `/cmd`, `/eye`, `/state`, `/idle`, `/speed`, `/backlight`,
    `/redraw`, `/canvas`, `/draw/clear`, `/draw/stroke`, `/char`, `/`.
- Offline frame-accurate preview player (`preview.html`): all 15 expressions,
  firmware-rendered frames, play/pause and step-frame controls, no CDN.
- README figures rendered from the firmware frames (`docs/images/`).
- `examples/clawd-notify.py`: Claude Code hook → expression bridge (never
  blocks the session when the device is offline).
- GitHub Actions workflow compiling the firmware on every push/PR.
