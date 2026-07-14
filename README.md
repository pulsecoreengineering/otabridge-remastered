# OTABridge

OTABridge is an ESP32-based wireless firmware programmer for AVR boards (Uno, Nano,
Mega, and compatible targets) with a built-in web UI and a remote serial debug channel.

Flash a target over WiFi and watch its serial output through the reboot — no USB
programmer, no PC tethered to the bench.

**This is the MVP core.** Scope is deliberately small: flash + debug, done well.
SDK, companion app, device discovery, firmware signing, and relay access are
planned layers on top of this base (see Roadmap).

## Features

- STK500v1 (Uno/Nano-class) and STK500v2 (Mega-class) flashing
- Protocol auto-detection with v1 fallback, plus manual protocol selection
- Device signature detection against built-in + custom profiles, with a
  manual-override flow for unknown clones
- Optional page-by-page verification after write
- Remote serial debug console on a dedicated second UART — stays live while
  flashing, streamed to the browser over SSE, with bidirectional send
- Web UI served from the device (LittleFS), live progress over SSE
- First-boot provisioning AP (device name, secret, WiFi)
- `X-API-Key` auth on all mutating endpoints (key = device secret)
- Per-device mDNS name: `otabridge-XXXX.local`

## Hardware / Wiring

Default pins:

| Function | ESP32 pin | Notes |
| --- | --- | --- |
| Program TX → target RX | GPIO17 | STK500 channel (Serial2) |
| Program RX ← target TX | GPIO16 | |
| Target RESET | GPIO4 | pulsed low, then released to input |
| Debug RX ← target debug TX | GPIO32 | monitor channel (Serial1) |
| Debug TX → target debug RX | GPIO33 | |
| Status LED | GPIO2 | |

The two UARTs are fully independent: flashing on Serial2 never interrupts the
debug stream on Serial1.

## Quick Start

```bash
git clone https://github.com/magxTz/OTABridge.git
cd OTABridge
pio run --target upload
pio run --target uploadfs
```

First boot (or hold BOOT while powering on to re-enter setup):

1. Join AP `OTABridge-Setup-XXXX` (password `setup1234`)
2. Open `http://192.168.4.1`
3. Set device name, device secret, and your WiFi credentials → Save & Restart
4. Open `http://otabridge-xxxx.local` (or the device IP), enter your device
   secret as the API key, upload a `.hex`, flash

> Keep a copy of the device secret — it is the API key for every write
> operation. Re-provision (BOOT-hold) if it is lost.

## API Surface

Read-only: `GET /api/status`, `/api/info`, `/api/config`, `/api/profiles`,
`/api/debug/status`, `/api/relay/status`, plus SSE streams `/api/progress`
and `/api/debug/stream`.

Mutating (require `X-API-Key`):
`POST /api/upload-hex`, `/api/program?protocol=auto|v1|v2`, `/api/override`,
`/api/cancel`, `/api/restart`, `/api/config`, `/api/config/reset`,
`/api/profiles` (+ `DELETE`), `/api/debug/start|stop|send`.

CORS is same-origin by design: the UI is served by the device itself.

## Known Limits (MVP)

- Setup AP password is a shared default until per-unit provisioning labels
  exist. Provision in a trusted environment.
- WiFi credential changes (setup page or Config tab) take effect after restart.

## Project Layout

```text
src/core/            # translation units + global state
src/modules/         # concern-based implementation (.inl per module)
include/otabridge/   # public headers, OTABRIDGE_FW_VERSION
data/index.html      # web UI served from LittleFS
relay/               # cross-network control-plane backend (accounts, device
                      # claim flow, Ed25519 device auth) — deploys separately
companion-app/       # PWA (Vite+React), deploys to Vercel — talks to the
                      # relay over HTTPS/WSS only, never touches a device's LAN
platformio.ini
```

## Roadmap

- TypeScript SDK (`otabridge-client`) generated against a frozen OpenAPI v1 spec
- Deployable companion app for multi-device benches
- LAN discovery (per-device mDNS is already in; enumeration layer to follow)
- Relay transport for off-LAN access — **in progress**: control plane (`/relay`)
  and firmware-side registration/claim/Ed25519-auth (`06_relay_client.inl`)
  are in; command/data-plane proxying (program/debug/progress over the relay)
  is not yet built
- Ed25519 signed-firmware verification (real asymmetric signing) — the device
  identity keypair above is a separate use of Ed25519 from this; firmware
  signing itself is still unbuilt
- ESP32 self-OTA (current binary: ~907KB of a 1.25MB OTA slot — fits)

## License

See `LICENSE` at the repository root.
