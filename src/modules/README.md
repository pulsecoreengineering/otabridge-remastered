# Firmware Module Layout

Concern-based modules, each included by exactly one translation unit in `src/core/`.

## Files

- `02_runtime_io.inl` (→ `core/RuntimeIO.cpp`)
  - SSE helpers (log / progress / state / device events)
  - Debug serial reader (Serial1, GPIO32/33)
  - Profile load/save helpers
- `03_protocol_flash.inl` (→ `core/ProtocolFlash.cpp`)
  - Intel HEX parser
  - STK500v1 / STK500v2 protocol classes
  - Flash task pipeline (page-bitmap streaming design)
- `04_storage_security_web.inl` (→ `core/WebApi.cpp`)
  - Config I/O
  - API key / CORS helpers
  - Main web API routing
- `05_provision_wifi_entry.inl` (→ `core/AppEntry.cpp`)
  - Provision AP page/server
  - Wi-Fi startup + setup-AP fallback
  - Serial CLI
  - `setup()` / `loop()`
- `06_relay_client.inl` (→ `core/RelayClient.cpp`)
  - Ed25519 device identity (generate on first boot, persist to LittleFS)
  - `/devices/register` + claim-code flow against the relay service (see `/relay`)
  - `/ws/device` challenge-response presence connection

Global state lives in `core/AppState.cpp`, declared in `include/otabridge/AppState.h`.
`OTABRIDGE_FW_VERSION` in `AppState.h` is the single version source of truth.

## Adding a new target protocol (e.g. STM32)

- Create `06_protocol_stm32.inl` and a matching `core/ProtocolStm32.cpp`
- Add protocol-specific flashing class(es)
- Add protocol selection hooks in the flash task orchestration
- Keep the API contract in `04_storage_security_web.inl` stable
