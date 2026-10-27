# Clawd Mochi Plus

**An enhanced open-source firmware for the Clawd Mochi desk companion** — 15 parametric geometric expressions, a runtime-adjustable eye geometry engine, an idle life state machine, and STA-first WiFi with AP provisioning. No bitmap assets, no private config files; open, compile, and flash.

This project is an independent firmware enhancement derived from [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) (MIT). It keeps the original hardware target and instant-on experience while adding a richer expression system for modern AI coding companions.

<p align="center"><img src="docs/images/expressions-demo.gif" alt="Clawd Mochi Plus expression demo — all 15 faces"></p>

- 🇨🇳 中文文档: [README.md](README.md)
- 🎬 Offline preview: open `preview.html` in any browser (no hardware needed)

---

## Features

- **15 expressions, pure math** — every expression is drawn at runtime from `(eyeW, eyeH, eyeGap, eyeOx, eyeOy)` with `Adafruit_GFX` primitives. Zero ROM bitmaps, zero RAM frame buffers.
- **Adaptive geometry engine** — resize/reposition the eyes via the `/eye` HTTP API; all 15 expressions rescale proportionally. Values persist to NVS with an explicit `save=1`.
- **Idle life machine** — randomized blinking (2.5–6 s), random left/right glances (6–15 s), a brief `> <` squint every 30–60 s, and a 5-minute nap (`ZZ` + dozing bars) after inactivity.
- **STA-first WiFi with AP provisioning** — first boot starts the `ClaWD-Mochi` hotspot (`clawd1234` / `http://192.168.4.1`). Enter your 2.4 GHz credentials at `/wifi`; they persist in NVS and the device joins your network as `http://clawd-mochi.local` via mDNS. If boot cannot connect, the AP stays up and STA keeps retrying in AP+STA.
- **Minimal built-in web controller** — mobile-friendly page on the device: 15 expression buttons, AP/STA link status, backlight toggle, and a `WiFi 设置` link. The `/eye` geometry API remains available over HTTP.
- **Offline frame-accurate preview** — `preview.html` replays firmware-rendered frames of all 15 expressions with play/pause and step-frame controls. Pure local, no CDN.

## Hardware

| Part | Detail |
| --- | --- |
| MCU | ESP32-C3 Super Mini (ESP32-C3, 160 MHz) |
| Display | 1.54" ST7789 SPI TFT, 240×240, 3.3 V logic |
| Wiring | SDA→GPIO 10 (MOSI), SCL→GPIO 8 (SCK), RST→GPIO 2, DC→GPIO 1, CS→GPIO 4, BL→GPIO 3, VCC→**3.3 V**, GND→GND |

> ⚠️ **Electrical safety**: the display VCC must go to **3.3 V only** — connecting 5 V can destroy the panel.
>
> ⚠️ **SPI pins**: use GPIO 8/10 for SCK/MOSI. Do **not** use GPIO 6/7 (they are reserved for the SPI flash on many C3 modules and will brick the boot).
>
> ⚠️ **WiFi TX power**: the firmware sets the radio to **8.5 dBm** at boot (`WiFi.setTxPower()` re-applied after every `WiFi.mode()` — the call is a no-op before the radio starts). Cheap ESP32-C3 Super Mini boards are known to emit a degraded spectrum at the default max TX power: APs cannot hear auth/assoc frames (`AUTH_EXPIRE`), and the provisioning AP beacon becomes unreliable/invisible. Verified 2026-10-19: at 8.5 dBm both STA and soft-AP connect and hold reliably. If your board has a proper antenna, you may raise `WIFI_POWER_8_5dBm` in `clawd_mochi.ino` for extra range.

3D-printed case: see the original author's model on MakerWorld — [Clawd Mochi physical Claude Code mascot](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000). This repository intentionally ships **no STL/3MF files** — print the original case and drop this firmware in.

## Build & Flash

**Option A — Arduino IDE 2.x**

1. Add Espressif's Boards Manager URL (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`) and install `esp32` **3.3.12**.
2. Install `Adafruit GFX Library` **1.12.6** and `Adafruit ST7735 and ST7789 Library` **1.11.0** via Library Manager.
3. Open `clawd_mochi/clawd_mochi.ino`, select board **ESP32C3 Dev Module** with:
   - CPU Frequency: **160 MHz**
   - USB CDC On Boot: **Enabled**
   - Upload Speed: **921600**
4. Flash the firmware.

**Option B — arduino-cli**

```powershell
$env:ESP32_INDEX = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
arduino-cli core update-index --additional-urls $env:ESP32_INDEX
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $env:ESP32_INDEX
arduino-cli lib install "Adafruit GFX Library@1.12.6" "Adafruit ST7735 and ST7789 Library@1.11.0"
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 clawd_mochi
arduino-cli upload --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 -p COM5 clawd_mochi
```
Replace `COM5` with the serial port shown by your operating system.

No configuration file is required for a default build — the first boot starts the AP provisioning hotspot. There is no `#error` gate and no `wifi_config.h` to create.

## Expressions

15 expressions, driven from HTTP (`/cmd?k=<key>`) or the built-in web controller.

<p align="center"><img src="docs/images/expressions-overview.png" alt="All 15 expressions with names"></p>

| # | Expression | Key | Alias | Frames | Frame period | Behavior | Typical use |
| :-: | --- | :-: | :-: | :-: | :-: | --- | --- |
| 1 | Normal | `w` | `normal` | 1 | — | persistent view, reacts to idle blink/glance | idle / standby |
| 2 | Happy | `h` | `happy` | 1 | — | holds, auto-restores | praise / success |
| 3 | Sleepy | `z` | `sleepy` | 2 | 600 ms | loops; dozing bars + rising `zZ` | late night / wind-down |
| 4 | Surprised | `o` | `surprised` | 5 | 60 ms | one-shot shake, enlarged eyes | listening / startled |
| 5 | Wink | `k` | `wink` | 1 | — | holds, auto-restores | playful moment |
| 6 | Dizzy | `x` | `dizzy` | 5 | 60 ms | one-shot sway, `X X` eyes | error / stuck / confusion |
| 7 | Angry | `g` | `angry` | 1 | — | holds, auto-restores | warning |
| 8 | Heart | `l` | `heart` | 2 | 250 ms | loops, pulsing heart eyes | task success / reward |
| 9 | Think | `t` | `think` | 4 | 140 ms | loops, flowing wave bars | analysis / reasoning |
| 10 | Executing | `e` | `executing` | 3 | 200 ms | loops, both eyes erode inward | code running / tool call |
| 11 | Crying | `c` | `crying` | 4 | 200 ms | loops, alternating teardrops | serious failure |
| 12 | Shocked | `b` | `shocked` | 2 | 90 ms | loops, oversized jittering eyes | unexpected result |
| 13 | Cool | `v` | `cool` | 2 | 350 ms | loops, sunglasses + moving glare | build passed / shipped |
| 14 | Embarrassed | `m` | `embarrassed` | 2 | 400 ms | loops, averted gaze + blush | permission request / shy |
| 15 | Delicious | `y` | `delicious` | 4 | 200 ms | loops, drooling drip | food / treats |

**Squeeze eyes (`s`)** — the classic `> <` view — is a **separate always-on view**, not part of the 15-expression set. It stays on screen until you switch away.

`/cmd?k=` accepts the single-letter keys above and the full-word aliases: `normal`, `happy`, `sleepy`, `surprised`, `wink`, `dizzy`, `angry`, `heart`, `think`, `executing`, `crying`, `shocked`, `cool`, `embarrassed`, `delicious`, `squish`. The key `n` silently restores normal eyes in the normal view; it does not leave the squish view. Unknown or empty keys return HTTP **400** `{"error":"unknown key"}`.

Temporary expressions auto-restore after 5 s (Think: 6 s). Pass `lease=<ms>` (500–1800000, i.e. up to 30 minutes) to hold longer. Normal eyes and squish ignore the lease.

## REST API

Base URL: `http://192.168.4.1` (AP) or `http://clawd-mochi.local` / the DHCP address (STA). All endpoints below return JSON unless noted.

> Network scope: the HTTP control API has no authentication or encryption. Use it only on a trusted private network, and do not expose the device to the public internet. WiFi credentials are submitted through the HTTP provisioning form and stored in the device's NVS.

| Endpoint | Purpose |
| --- | --- |
| `/cmd?k=<key>` | Trigger an expression key or alias (see table above). Optional `lease=<ms>` (500–1800000) for temp expressions. HTTP 400 on an unknown key. |
| `/eye?w=&h=&gap=&ox=&oy=` | Adjust eye width/height, center distance, and X/Y offset. Add `save=1` to persist to NVS; `reset=1` restores defaults. |
| `/state` | Current expression, idle/backlight flags, eye geometry, animation speed, animation background (`bg`), WiFi mode (`ap`/`sta`), IP, hostname, RSSI (STA only), free heap. Sent with `Cache-Control: no-store`. |
| `/wifi` | `GET` returns the provisioning form; `POST ssid=&pass=` saves credentials to NVS and reboots. Empty SSID returns HTTP 400. |
| `/health` | `{"ok":true}` with `Cache-Control: no-store`. |
| `/idle?on=1\|0` | Enable/disable the idle life machine (persisted immediately). |
| `/speed?v=1\|2\|3` | Intro animation speed: 1 = slow (default), 2 = normal, 3 = fast. It does not change expression frame periods. |
| `/backlight?on=1\|0` | Screen backlight on/off. |
| `/redraw?bg=<RRGGBB>` | Set animation background color and redraw. Invalid colors return HTTP 400. |

Example:

```bash
curl "http://192.168.4.1/cmd?k=crying"
curl "http://192.168.4.1/cmd?k=e&lease=1800000"
curl "http://192.168.4.1/eye?w=36&h=72&gap=128&oy=36&save=1"
curl "http://192.168.4.1/state"
curl "http://192.168.4.1/health"
```

## Drive it from Claude Code (example)

Any HTTP client can drive the face:

```bash
curl "http://192.168.4.1/cmd?k=e"   # executing
curl "http://192.168.4.1/cmd?k=l"   # heart — all done
```

`examples/clawd-notify.py` maps Claude Code hook events to faces. Suggested mapping (edit to taste):

| Claude Code hook event | Key | Lease (ms) | Face |
| --- | :-: | ---: | --- |
| `UserPromptSubmit` | `t` | 600000 | think |
| `PreToolUse` | `e` | 1800000 | executing |
| `PostToolUse` | `t` | 600000 | think |
| `SubagentStart` | `t` | 600000 | think |
| `SubagentStop` | `t` | 600000 | think |
| `PostToolUseFailure` | `x` | — | dizzy |
| `PermissionRequest` | `m` | 600000 | embarrassed |
| `Notification` | `m` | 600000 | embarrassed |
| `Stop` | `l` | — | heart |
| `StopFailure` | `c` | — | crying |
| `SessionEnd` | `n` | — | back to normal |

Wire it into your Claude Code `settings.json` (`hooks` section), e.g. for every tool call:

```json
{
  "hooks": {
    "PreToolUse": [
      { "matcher": "*",
        "hooks": [ { "type": "command",
                     "command": "python /path/to/examples/clawd-notify.py", "timeout": 5 } ] }
    ]
  }
}
```

The script defaults to `http://clawd-mochi.local`. If mDNS is unavailable, set `CLAWD_URL` to the device IP (for example `http://192.168.4.1` during first-boot AP provisioning). The script uses a 0.4 s network timeout; connection errors are ignored.

## WiFi Modes

**First boot / no saved credentials — provisioning hotspot.** The device starts in AP mode:

- SSID `ClaWD-Mochi`, password `clawd1234`, page at `http://192.168.4.1`
- Open `http://192.168.4.1/wifi`, enter a 2.4 GHz SSID and password, and submit. Credentials are stored in NVS (`Preferences`, namespace `wifi`) and the device reboots.

**After provisioning — home network (STA).** The device boots as a station, gets a DHCP address, and advertises `http://clawd-mochi.local` over mDNS. Boot waits up to 15 s for the connection; if it fails, the AP hotspot comes up in AP+STA mode and STA keeps retrying so you can re-provision.

**Runtime reconnect.** A 10 s watchdog calls `WiFi.reconnect()` on drop. The AP is only started at boot; if the router password changes while the device is running, reboot it to return to the boot fallback AP.

Notes: 2.4 GHz networks only; there is no static-IP configuration — the router's DHCP assigns the address; mDNS is best-effort and does not block boot if it fails.

## Offline Preview (`preview.html`)

Double-click `preview.html` — a single-file HTML5 player with every expression pre-rendered frame-by-frame from the firmware. Play/pause, single-frame stepping, per-expression frame counts and periods. Completely local: no CDN, no build step, no network requests.

Note: frames are pre-generated PNG matrices, so they do not react to `/eye` geometry changes — for live geometry tuning use the `/eye` HTTP API.

## Project Layout

```text
clawd-mochi-plus/
├── clawd_mochi/
│   └── clawd_mochi.ino        # complete firmware (single file)
├── examples/
│   └── clawd-notify.py        # Claude Code hook → expression bridge (example)
├── docs/images/               # README figures (rendered from firmware frames)
├── preview.html               # offline expression frame player
├── .github/workflows/compile.yml   # CI: compiles the firmware on every push
├── AGENTS.md                  # conventions and architecture notes for contributors/AI
├── CHANGELOG.md
├── LICENSE                    # MIT (original + Plus contributors)
└── README.md / README_en.md   # Chinese (default) / English
```

## Credits & License

- Original project: [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) — hardware design, base firmware, 3D case ([MakerWorld](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)).
- Clawd Mochi Plus: expression engine, adaptive geometry, idle state machine, STA-first WiFi with AP provisioning, offline preview.

MIT — see [LICENSE](LICENSE).
