# OTABridge Relay

Control-plane backend for cross-network access: accounts, device registration,
and the claim flow that binds a device to an account. This is what lets the
companion app (and eventually the PWA) reach a device regardless of which
network either side is on, without the browser ever touching the LAN directly.

**What this is not (yet):** command/data-plane proxying — routing an actual
`program`/`debug` request or a live progress/debug stream from the companion
app through to a specific device's socket. That's a deliberate follow-up
phase; this package only establishes and tracks authenticated presence.

## Why it exists

A browser page served from a public host (e.g. a Vercel-hosted PWA) cannot
reach a LAN device directly — mixed content blocks HTTPS-page-to-HTTP-device
fetches, and CORS blocks cross-origin calls even if that didn't. The fix is
routing everything through a service both sides reach over outbound HTTPS/WSS:
the device connects out to the relay, the app connects out to the relay, the
relay pairs them up. See the project conversation history for the full
architecture discussion.

## Credential model

Three tiers, kept deliberately separate:

1. **Local API key** (`X-API-Key`, unchanged) — still used for direct-LAN
   access to a device's own web UI/API. The relay never sees or handles this.
2. **Device ↔ relay** — each device generates an Ed25519 keypair on first
   boot (implemented — see `src/modules/06_relay_client.inl`, keypair
   persisted to `/relay_identity.json` on LittleFS). The public key is
   registered with the relay; the private key never leaves the device. Every `/ws/device`
   connection is authenticated by signing a server-issued nonce — proof of
   possession per-connection, nothing long-lived to leak.
3. **Companion app ↔ relay** — normal account auth (email/password + JWT for
   now). The app never sees a device's local API key or private key; the
   relay checks account ownership before allowing anything.

## Claim flow

1. Device finishes Wi-Fi provisioning (existing captive-portal flow,
   unchanged) and successfully joins the real network, then
   `POST /devices/register` with its device ID (the full 6-byte factory MAC,
   hex-encoded — a separate, larger namespace from the 4-char
   `deviceIdSuffix()` used for the LAN mDNS name), Ed25519 public key, and
   name. This can only happen once WiFi is actually up, which rules out
   showing the code on the setup AP's captive-portal page (`192.168.4.1`) —
   that page is served before the device has left AP mode.
2. If the device is new/unclaimed, the relay returns a short-lived (15 min),
   single-use claim code (`XXXX-XXXX`, ambiguous characters excluded). The
   device prints this to the serial console on connect, and it stays visible
   at `GET /api/relay/status` on the device's normal web API for the rest of
   the claim window — v1 is manual entry, no QR yet.
3. User signs up / logs in on the companion app, enters the code via
   `POST /devices/claim` (account-authenticated). The relay verifies the code,
   marks the device claimed, and creates an `AccountDevice` row.
4. From then on the device authenticates over `/ws/device` via challenge-
   response; the app lists its devices via `GET /devices` and opens
   `/ws/app?token=<jwt>` for presence.

`AccountDevice` is a join table even though v1 enforces exactly one owner per
device in application logic — this is so shared/team device access can land
later without a schema rewrite.

## Local development

```bash
cp .env.example .env   # fill in DATABASE_URL / JWT_SECRET
npm install
npx prisma migrate dev
npm run dev
```

Requires a Postgres instance reachable at `DATABASE_URL` — Railway's managed
Postgres is the intended target in production; anything Postgres-compatible
works locally (a local install, or `docker run postgres`).

## Routes

| Route | Auth | Purpose |
| --- | --- | --- |
| `POST /auth/signup` | — | Create an account, returns a JWT |
| `POST /auth/login` | — | Returns a JWT |
| `POST /devices/register` | — (called by the device itself) | Register/re-register a device, get a claim code if unclaimed |
| `POST /devices/claim` | Bearer JWT | Claim a device by code |
| `GET /devices` | Bearer JWT | List the account's devices with online status |
| `WS /ws/device` | Ed25519 challenge-response | Device presence channel |
| `WS /ws/app?token=` | JWT (query param) | Companion app presence channel |

The device's own `GET /api/relay/status` (on the device's normal web API, not
this service) mirrors registration/claim/connection status locally — useful
since the claim code only lives in the boot-time serial log otherwise.

## Known gaps / next phases

- No command/data-plane relay yet (program/debug/progress proxying between a
  specific app session and a specific device socket).
- Presence registry is in-memory — fine for a single instance; a
  multi-instance deployment needs a shared layer (e.g. Redis pub/sub) to route
  across processes.
- Firmware side is implemented (`src/modules/06_relay_client.inl`) but
  untested against a real device — this package's routes were verified with
  a real Postgres instance and real Ed25519 keypairs from a Node script; the
  firmware side hasn't been compiled/flashed to hardware yet (no ESP32
  toolchain in the environment that wrote it). Set
  `OTABRIDGE_RELAY_HOST`/`OTABRIDGE_RELAY_PORT` (see `include/otabridge/AppState.h`)
  to your deployed Railway hostname before building.
- Firmware skips TLS certificate validation on both the register call and the
  WS connection (see the note at the top of `06_relay_client.inl`) — fine for
  bench testing, not for a device leaving a trusted network.
- Account auth is email/password only; OAuth can be added later without
  touching the device/claim model, since it's orthogonal.
