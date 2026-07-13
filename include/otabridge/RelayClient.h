#pragma once

#include "otabridge/AppState.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <Ed25519.h>
#include <RNG.h>

// Loads the device's Ed25519 keypair from LittleFS, generating one on first
// boot. Call before registerWithRelay().
bool loadOrCreateRelayIdentity();

// POSTs to the relay's /devices/register (blocking, ~8s timeout). Safe to
// call once per successful WiFi connection — a no-op response is fine if the
// relay is unreachable, the device just stays local-only for this session.
void registerWithRelay();

// Drives the /ws/device challenge-response connection. Call from loop() in
// normal (non-provisioning) mode; internally a no-op until registerWithRelay()
// has succeeded at least once.
void relayLoop();
