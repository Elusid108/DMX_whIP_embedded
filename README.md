# DMX_whIP_embedded

Version: **0.3.0**

The embedded side of DMX_whIP: firmware for pixel nodes that will receive live Art-Net / sACN (KiNet later) and play recorded frames from SD. This tree is shared across boards. Current hardware is a **Waveshare ESP32-S3-Matrix** bring-up node, not the production controller.

The 18-month-old [`Esp32-s3 Pixel Playback — Rebuild Plan (step-by-step).pdf`](Esp32-s3%20Pixel%20Playback%20%E2%80%94%20Rebuild%20Plan%20(step-by-step).pdf) is **historical**. Do not copy its `platformio.ini` starter (`qspi_opi`, 8MB-class flash, SDMMC, huge rings, AsyncWebServer + LittleFS SPA). This README is the living plan.

## Current hardware (`[env:matrix]`)

- MCU: ESP32-S3FH4R2 — **4MB flash, 2MB QSPI PSRAM** (`qio_qspi`, `default.csv`)
- LED: 8×8 WS2812B on GPIO 14, GRB, default brightness **10/255** (portal 0–255; warn above 64 — this panel can overheat)
- USB-C: native USB-Serial/JTAG (`ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1`)
- microSD: **SPI** CS 7, MOSI 6, CLK 5, MISO 4 (3.3 V module only)

Prove the **pipeline** here with 64 pixels. Retarget later with a new PlatformIO env and a board profile (pins, flash, PSRAM, LED count, ring sizes). Share `src/`; do not fork the tree. One node is not 100k live pixels.

## Flash and serial

Every upload: hold **BOOT**, tap **RESET**, release **BOOT**, then `pio run -e matrix -t upload`. Serial window: `ESP32 COM3` via [`scripts/serial-monitor.ps1`](scripts/serial-monitor.ps1). Details are in [`.cursor/rules/esp32-matrix.mdc`](.cursor/rules/esp32-matrix.mdc).

## SoftAP config portal

- SSID `dmxwhip`, password `pass1234`, page `http://4.3.2.1`
- AP+STA: scan 2.4 GHz networks, connect, always **remember** last STA network in NVS and reconnect at boot. **Forget saved network** on the page clears NVS and drops STA. SoftAP/HTTP stay up while idle (no live lighting protocol). When `LiveInput` is fresh (Art-Net today; sACN/KiNet later), SoftAP and the portal stop so the radio is STA-only. After ~2 s of silence the portal comes back. SoftAP also returns if STA drops.
- Captive DNS hijacks all names to `4.3.2.1`. Probe URLs (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, …) return the portal HTML with **200**, never OS “success” tokens (no HTTP 204 for Android, no Apple `Success`, no Windows NCSI pass string).
- Limit: HTTPS connectivity checks cannot be spoofed. Some new phones only show a sign-in notification. DHCP Captive-Portal-API (RFC 8910) needs IDF 5+; this Arduino core is IDF 4.4. If the sheet does not open, use `http://4.3.2.1` (not https).
- Connecting STA may hop the SoftAP channel; if the page drops, rejoin `dmxwhip`. While a protocol is live the AP is gone — unicast to the **STA IP** (serial `[V][wifi] connected ... ip=`). While idle, use `dmxwhip` / `http://4.3.2.1`.
- Max brightness slider 0–255 (default 10, stored in NVS). Warning on the page above 64; the value is not capped. `/status` field `bri`. Live Art-Net uses this as FastLED global scale.
- SD line on the page from cached mount stats: type, size MB, used, free (or `not mounted`). `/status` object `sd`.

## Art-Net (Resolume)

- UDP **6454**, universe **0** (Resolume “universe 1” is often Art-Net 0). 64 pixels = 192 RGB channels, **1:1** onto the strip (DMX triplet *n* → LED *n*). No serpentine/row remap in firmware — put snake/orientation in the Resolume fixture patch. FastLED maps RGB → GRB. Brightness default 10 (portal cap).
- Live path: drop-to-latest (overwrite unread frames). Idle is **black** (no rainbow). Unicast from Resolume to the STA IP. Serial `[V][artnet]` first packet + 5 s counters (`drops` = overwritten before render). `[V][ap] down (live)` / `[V][ap] up (idle)` on portal transitions.

## Living milestone list

Do not delete lines. `[ ]` not done. `[x]` implemented. `[x] **verified**` only after hardware confirmation.

Bring-up (this board)

- [x] **verified** — Serial logging (`Log` / `LOG_V` / `LOG_C`, RAM replay, `logsvc`)
- [x] **verified** — SoftAP `dmxwhip` + config page scan / connect
- [x] LED rainbow smoke test (GPIO 14, GRB, brightness 10) — implemented
- [x] SD SPI mount + root listing — implemented (not verified)
- [x] STA credentials persist in NVS and reconnect at boot — implemented
- [x] Captive probe handlers + viewport-fixed portal + Forget + version in `/status` — implemented (this rev; captive auto-open not verified)
- [x] SoftAP IP `4.3.2.1` (DHCP gateway + DNS) — implemented
- [x] SoftAP stops ~45 s after STA IP (STA-only for live); AP returns if STA drops — implemented
- [x] Idle = black panel + SoftAP/HTTP; live protocol = pixels + portal down; AP returns after ~2 s silence — implemented (not verified)
- [x] Live Art-Net 1:1 channel → LED index (no firmware snake remap) — implemented (not verified)
- [x] Portal max brightness 0–255 (default 10, NVS, warn >64) — implemented (not verified)
- [x] SD mount/size/used/free on portal `/status` — implemented (not verified)

From the historical PDF (adapted)

- [ ] Board profile as data (pins, flash, PSRAM, LED count, ring sizes); extra PlatformIO envs without forking `src/`
- [ ] JSON `/api/stats` (Wi-Fi IP/RSSI, later queue depths / drops / FPS)
- [ ] RTOS layout: RX on APP CPU, render on PRO CPU, SD I/O task; rings allocated at boot
- [ ] Protocol adapter interface + DMX universe assembler (seq, late, missing, dupes)
- [x] Art-Net (UDP 6454) → assembler → test pattern / 64 pixels — implemented (universe 0 → 8×8; no multi-universe assembler yet; not verified)
- [ ] sACN / E1.31 multicast + frame fence
- [ ] KiNet (optional; after Art-Net and sACN)
- [x] LED manager: (universe, channel) → framebuffer; double-buffer (triple if SD + net) — implemented (64-pixel drop-to-latest + 25 ms show; not a full LED manager)
- [x] Render scheduler at target FPS; drop-to-latest for live — implemented (40 FPS / 25 ms; not verified)
- [ ] SD async reader (SPI on this board; SDMMC only on boards that have it); ring sized from profile — not 128–512 KB on the Matrix
- [ ] Recording file spec v1 (header + timestamped frames + CRC + index)
- [ ] Playback engine; pause on underrun
- [ ] Web UI beyond SoftAP (protocol + playback + stats). Stay on PROGMEM/`WebServer` until the UI outgrows it; no AsyncWebServer / LittleFS SPA yet
- [ ] Watchdog + `/api/logs`; soak test

Companion PC (sibling repo, not this tree)

- [ ] Art-Net / sACN test sender
- [ ] Later: recorder and SD file pull; node exposes the files/API this app will use

## Architecture (keep)

- Incremental, testable milestones
- One firmware tree; board profile is data
- Live: drop-to-latest. Playback: pause on underrun
- Observability: tagged logs now; stats/API later
- Defer AsyncWebServer, ArduinoJson, LittleFS SPA, huge lwIP rings, KiNet until Art-Net then sACN work on this node

## Version history

- **0.3.0** — Portal max brightness 0–255 (default 10, NVS, warn >64); SD type/size/used/free on the config page
- **0.2.2** — Live Art-Net is 1:1 onto the strip; serpentine remap removed so Resolume owns the fixture patch
- **0.2.1** — Black idle; SoftAP/HTTP only while not streaming (`LiveInput`); Art-Net still the only live source
- **0.2.0** — Art-Net universe 0 → 8×8; SoftAP off 45 s after STA; drop-to-latest live render; rainbow if stream silent
- **0.1.1** — SoftAP IP / fallback `http://4.3.2.1` (WLED-style); DNS TTL 0
- **0.1.0** — Bring-up: serial log, SD SPI, LED rainbow, SoftAP portal (`dmxwhip` / `pass1234`), NVS STA remember, captive probes, Forget, version in `/status`
