# OTABridge Companion App

Pure PWA — no native shell, deployed as a static site (Vercel is the intended
target). Talks to the relay (`/relay`) over HTTPS/WSS only; never touches a
device's LAN directly. See the project conversation history for why (mixed
content + CORS make direct browser-to-LAN-device access unreliable, and the
relay exists specifically to route around that).

## Scope (v1)

- Sign up / log in (email + password against the relay's account system)
- Claim a device by the code it prints on its serial console after joining WiFi
- Live device view: status, progress, cancel/restart, a debug console
  (start/stop, live line feed, send text to the target)
- **Bounded remote flashing** — upload a `.hex` file, chunked and sent over
  the relay WS to `program_start`/`program_chunk`/`program_end`. Capped at
  131072 bytes of hex *text* (`MAX_HEX_BYTES` in `DeviceDetailPage.tsx`, must
  match `RELAY_PROGRAM_MAX_HEX_BYTES` in `06_relay_client.inl`) — covers
  Uno/Nano/Pro Mini/Leonardo/Pro Micro-class boards even fully packed. Mega
  needs streaming ingest first; not attempted here, matches the plan to
  validate small boards before circling back.

## Structure

```text
src/api/client.ts       # REST calls (auth, device list, claim) + token storage
src/api/relaySocket.ts  # WS wrapper — subscribe, and sendCmd() as a promise
                         # resolved/rejected by the matching cmd_result
src/pages/               # LoginPage, DevicesPage, DeviceDetailPage
src/App.tsx              # state-based view switching — no router, only 3 views
```

No SDK package — this talks to the relay's REST/WS API directly. Worth
extracting into a shared `otabridge-client` package (the roadmap's planned
TypeScript SDK) once there's a second consumer of that API surface; one
consumer doesn't need a published package yet.

## Local development

```bash
cp .env.example .env.local   # or export VITE_RELAY_URL
npm install
npm run dev
```

`VITE_RELAY_URL` defaults to the deployed Railway relay if unset.

## Deploying to Vercel

Standard Vite static site — Vercel auto-detects the framework. Set
`VITE_RELAY_URL` as a Vercel project environment variable if it should point
somewhere other than the default baked into `src/api/client.ts`.

## Known gaps

- No SDK/shared client package yet (see above).
- No device rename/remove UI — only claim + list.
- Debug console's "active" state is tracked client-side from button presses,
  not confirmed against the device's actual state — good enough for v1, but
  worth reconciling against a `status` push if it drifts in practice.
- Verified in this environment: TypeScript build, and UI rendering/error
  handling via a real headless-browser pass (login form, devices list with a
  fake token) — this sandbox's outbound network policy blocks the live relay
  domain, so the actual signup/login/claim/WS round trip against production
  has not been exercised from here. Needs a real run against the deployed
  relay to confirm end-to-end.
