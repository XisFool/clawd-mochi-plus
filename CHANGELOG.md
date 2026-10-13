# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
