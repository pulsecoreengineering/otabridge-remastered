// ─── Relay Client ────────────────────────────────────
// Cross-network device<->companion-app access via the relay service (see
// /relay). Three responsibilities: Ed25519 identity persistence, the
// /devices/register + claim-code flow, and the /ws/device challenge-response
// presence connection.
//
// Device<->relay trust is Ed25519, deliberately independent of
// provision.deviceSecret (the local API key): the private key never leaves
// this device, and every /ws/device connection is authenticated by signing a
// server-issued nonce rather than presenting a long-lived shared secret.
//
// Both the HTTPS register call and the WSS connection below validate the
// relay's TLS certificate against a pinned root CA (see RelayCA.h) rather
// than trusting any certificate — this matters even on top of the Ed25519
// handshake, since /devices/register's response carries the claim code in
// the clear, and an on-path attacker on an untrusted network could otherwise
// intercept it and claim the device before its rightful owner does.

static uint8_t relayPrivateKey[32];
static uint8_t relayPublicKey[32];
static WebSocketsClient relayWs;
static bool relayWsStarted = false;

// Relay-mediated `program` — chunks stream straight to the hexIngest file on
// LittleFS (see 03_protocol_flash.inl), same as the local /api/upload-hex
// path, so this cap is just a sanity ceiling rather than a RAM constraint.
// Sized for the largest BUILTIN_PROFILES flash (Mega 2560, 256KB) at Intel
// HEX's worst-case ~2.8x text expansion (16 data bytes -> a 45-char line):
// 256KB * 2.8 =~ 737KB, rounded up with headroom.
#define RELAY_PROGRAM_MAX_HEX_BYTES (800 * 1024)
static bool   relayProgramReceiving      = false;
static size_t relayProgramBytesReceived  = 0;  // tracked locally — cheaper than re-statting LittleFS per chunk

static void relayBytesToHex(const uint8_t* bytes, size_t len, char* out) {
    static const char* hexchars = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hexchars[bytes[i] >> 4];
        out[i * 2 + 1] = hexchars[bytes[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static bool relayHexToBytes(const String& hex, uint8_t* out, size_t outLen) {
    if ((size_t)hex.length() != outLen * 2) return false;
    for (size_t i = 0; i < outLen; i++) {
        char hi = hex[i * 2], lo = hex[i * 2 + 1];
        int h = (hi >= '0' && hi <= '9') ? hi - '0' :
                (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 :
                (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
        int l = (lo >= '0' && lo <= '9') ? lo - '0' :
                (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 :
                (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return true;
}

// Full 6-byte factory MAC, hex-encoded — globally unique, unlike the 4-char
// deviceIdSuffix() used for the LAN mDNS name (fine at LAN scale, too small
// a namespace for a cross-account cloud registry).
static String relayFullDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char id[13];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(id);
}

bool loadOrCreateRelayIdentity() {
    if (LittleFS.exists("/relay_identity.json")) {
        File f = LittleFS.open("/relay_identity.json", "r");
        if (f) {
            DynamicJsonDocument doc(256);
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                String priv = doc["privateKey"] | "";
                String pub  = doc["publicKey"]  | "";
                if (relayHexToBytes(priv, relayPrivateKey, 32) &&
                    relayHexToBytes(pub, relayPublicKey, 32)) {
                    relayPublicKeyHex = pub;
                    return true;
                }
            }
        }
        Serial.println("[RELAY] identity file unreadable — regenerating");
    }

    // First boot (or corrupt identity file) — generate a fresh keypair.
    // Seed the library's RNG from ESP32's hardware TRNG (esp_random(), the
    // same source already trusted elsewhere in this codebase for
    // generateSecret()) rather than the library's default analog noise pin,
    // which this board doesn't wire up.
    RNG.begin("otabridge relay identity");
    uint8_t entropy[32];
    for (int i = 0; i < 8; i++) {
        uint32_t r = esp_random();
        memcpy(entropy + i * 4, &r, 4);
    }
    RNG.stir(entropy, sizeof(entropy), 256);

    Ed25519::generatePrivateKey(relayPrivateKey);
    Ed25519::derivePublicKey(relayPublicKey, relayPrivateKey);

    char privHex[65], pubHex[65];
    relayBytesToHex(relayPrivateKey, 32, privHex);
    relayBytesToHex(relayPublicKey, 32, pubHex);

    File f = LittleFS.open("/relay_identity.json", "w");
    if (!f) {
        Serial.println("[RELAY] identity: LittleFS write FAILED");
        return false;
    }
    DynamicJsonDocument doc(256);
    doc["privateKey"] = privHex;
    doc["publicKey"]  = pubHex;
    serializeJson(doc, f);
    f.close();

    relayPublicKeyHex = String(pubHex);
    Serial.println("[RELAY] generated new device identity");
    return true;
}

// ─── /devices/register ─────────────────────────────────
void registerWithRelay() {
    if (relayPublicKeyHex.length() == 0 && !loadOrCreateRelayIdentity()) {
        Serial.println("[RELAY] register: no identity — skipping");
        return;
    }
    relayDeviceId = relayFullDeviceId();

    WiFiClientSecure client;
    client.setCACert(OTABRIDGE_RELAY_CA_CERT);

    HTTPClient https;
    String url = String("https://") + OTABRIDGE_RELAY_HOST + ":" +
                 String(OTABRIDGE_RELAY_PORT) + "/devices/register";
    if (!https.begin(client, url)) {
        Serial.println("[RELAY] register: connection setup failed");
        return;
    }
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(8000);

    DynamicJsonDocument body(256);
    body["deviceId"]  = relayDeviceId;
    body["publicKey"] = relayPublicKeyHex;
    body["name"]      = provision.deviceName;
    String payload;
    serializeJson(body, payload);

    int code = https.POST(payload);
    if (code != 200) {
        Serial.printf("[RELAY] register failed: HTTP %d\n", code);
        https.end();
        return;
    }

    DynamicJsonDocument resp(256);
    DeserializationError err = deserializeJson(resp, https.getString());
    https.end();
    if (err) {
        Serial.println("[RELAY] register: bad JSON response");
        return;
    }

    relayRegistered = true;
    relayClaimed    = resp["claimed"] | false;

    if (!relayClaimed) {
        relayClaimCode = resp["code"] | "";
        Serial.println("=========================================");
        Serial.printf(" Relay claim code: %s\n", relayClaimCode.c_str());
        Serial.println(" Enter this in the companion app within 15");
        Serial.println(" minutes to link this device to your account.");
        Serial.println(" (Also visible at GET /api/relay/status.)");
        Serial.println("=========================================");
    } else {
        relayClaimCode = "";
        Serial.println("[RELAY] device already claimed");
    }
}

// ─── Command dispatch — cancel/restart/status/debug plus relay-mediated
// program_start/program_chunk/program_end (hex text streamed to LittleFS via
// hexIngest*(), see 03_protocol_flash.inl) ───────────────────────────
static void relaySendCmdResult(const String& requestId, bool ok, const char* message = "") {
    DynamicJsonDocument doc(256);
    doc["type"]      = "cmd_result";
    doc["requestId"] = requestId;
    doc["ok"]        = ok;
    if (message[0]) doc["message"] = message;
    String out;
    serializeJson(doc, out);
    relayWs.sendTXT(out);
}

// Lighter than the local /api/status (no busy/detectedSig/detectedProtocol
// formatting) — enough for a remote "what's it doing right now" check.
static void relaySendStatusSnapshot() {
    const char* s = "idle";
    switch (programmerState) {
        case STATE_IDLE:              s = "idle";              break;
        case STATE_LOADING_HEX:       s = "loading";           break;
        case STATE_ENTERING_PROGMODE: s = "entering_progmode"; break;
        case STATE_READING_SIGNATURE: s = "reading_signature"; break;
        case STATE_AWAITING_OVERRIDE: s = "awaiting_override"; break;
        case STATE_PROGRAMMING:       s = "programming";       break;
        case STATE_EXITING_PROGMODE:  s = "exiting";           break;
        case STATE_SUCCESS:           s = "success";           break;
        case STATE_ERROR:             s = "error";             break;
    }
    DynamicJsonDocument doc(320);
    doc["type"]      = "status";
    doc["state"]     = s;
    doc["page"]      = currentPage;
    doc["total"]     = totalPages;
    doc["lastError"] = lastError;
    String out;
    serializeJson(doc, out);
    relayWs.sendTXT(out);
}

static void relayHandleCommand(DynamicJsonDocument& doc) {
    String action    = doc["action"]    | "";
    String requestId = doc["requestId"] | "";

    if (action == "cancel") {
        // Mirrors POST /api/cancel.
        manualOverride.active = false;
        activeProfileIndex    = -1;
        if (flashTaskHandle != NULL) {
            cancelFlashRequested = true;
        } else {
            programmerState = STATE_IDLE;
            lastError = "Cancelled by user";
        }
        relaySendCmdResult(requestId, true);

    } else if (action == "restart") {
        // Mirrors POST /api/restart — ack first, then reboot.
        relaySendCmdResult(requestId, true);
        delay(500);
        ESP.restart();

    } else if (action == "status") {
        relaySendStatusSnapshot();
        relaySendCmdResult(requestId, true);

    } else if (action == "debug_start") {
        // Mirrors POST /api/debug/start.
        if (programmerState != STATE_IDLE && programmerState != STATE_SUCCESS &&
            programmerState != STATE_ERROR) {
            relaySendCmdResult(requestId, false, "Cannot start debug while programming");
        } else {
            debugBaudRate = doc["baud"] | 9600;
            Serial1.end();
            delay(50);
            Serial1.begin(debugBaudRate, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
            delay(50);
            while (Serial1.available()) Serial1.read(); // flush
            debugLineBuf = "";
            debugActive  = true;
            relaySendCmdResult(requestId, true);
        }

    } else if (action == "debug_stop") {
        // Mirrors POST /api/debug/stop.
        debugActive  = false;
        debugLineBuf = "";
        Serial1.end();
        relaySendCmdResult(requestId, true);

    } else if (action == "program_start") {
        // Mirrors the start of POST /api/upload-hex (opens the hexIngest
        // file) plus the busy-check from POST /api/program.
        if (programmerState != STATE_IDLE && programmerState != STATE_SUCCESS &&
            programmerState != STATE_ERROR) {
            relaySendCmdResult(requestId, false, "Programmer busy");
        } else {
            long totalBytes = doc["totalBytes"] | 0;
            if (totalBytes <= 0 || totalBytes > RELAY_PROGRAM_MAX_HEX_BYTES) {
                relaySendCmdResult(requestId, false, "Hex too large for relay upload");
            } else if (!hexIngestBegin()) {
                relaySendCmdResult(requestId, false, "Failed to open storage for hex data");
            } else {
                relayProgramBytesReceived = 0;
                activeProfileIndex     = -1;
                manualOverride.active  = false;
                relayProgramReceiving  = true;
                String proto = doc["protocol"] | "auto";
                if      (proto == "v1") preferredProtocol = PROTO_STK500V1;
                else if (proto == "v2") preferredProtocol = PROTO_STK500V2;
                else                    preferredProtocol = PROTO_UNKNOWN;
                relaySendCmdResult(requestId, true);
            }
        }

    } else if (action == "program_chunk") {
        if (!relayProgramReceiving) {
            relaySendCmdResult(requestId, false, "No program_start in progress");
        } else {
            String chunk = doc["data"] | "";
            // Defensive cap — program_start only checked the *declared*
            // total; don't trust it, enforce on what's actually arriving.
            if (relayProgramBytesReceived + chunk.length() > RELAY_PROGRAM_MAX_HEX_BYTES) {
                relayProgramReceiving = false;
                hexIngestEnd();
                hexIngestClear();
                relaySendCmdResult(requestId, false, "Exceeded max hex size — aborted");
            } else if (!hexIngestAppend((const uint8_t*)chunk.c_str(), chunk.length())) {
                relayProgramReceiving = false;
                hexIngestEnd();
                hexIngestClear();
                relaySendCmdResult(requestId, false, "Storage write failed (out of space?)");
            } else {
                relayProgramBytesReceived += chunk.length();
                relaySendCmdResult(requestId, true);
            }
        }

    } else if (action == "program_end") {
        // Mirrors POST /api/program's task kickoff — from here, the existing
        // sseState()/sseProgress() hooks already push to the relay as
        // flashTask runs, nothing extra needed.
        relayProgramReceiving = false;
        hexIngestEnd();
        if (hexIngestSize() == 0) {
            relaySendCmdResult(requestId, false, "No hex data received");
        } else {
            programmerState      = STATE_LOADING_HEX;
            lastError            = "";
            cancelFlashRequested = false;
            // Core 0, deliberately not core 1: loop() — which is what
            // actually pumps relayWs.loop(), since WebSocketsClient only
            // sends/receives when polled, unlike AsyncTCP's independent task
            // for the local web server — runs on core 1 by default. Sharing
            // that core with flashTask starved the WS connection badly
            // enough in practice that this cmd_result ack (and the
            // status/progress pushes during flashing) arrived many seconds
            // late or not at all, even though flashing itself proceeded
            // correctly on the device the whole time.
            xTaskCreatePinnedToCore(
                flashTask, "flashTask", 8192, NULL, 1, &flashTaskHandle, 0);
            relaySendCmdResult(requestId, true);
        }

    } else if (action == "debug_send") {
        // Mirrors POST /api/debug/send.
        if (!debugActive) {
            relaySendCmdResult(requestId, false, "Debug not active");
        } else {
            String cmd = doc["text"] | "";
            cmd.trim();
            if (cmd.length() > 0) {
                Serial1.print(cmd);
                Serial1.print('\n'); // Mega needs \n to terminate readStringUntil('\n')
            }
            relaySendCmdResult(requestId, true);
        }

    } else {
        relaySendCmdResult(requestId, false, "Unknown action");
    }
}

// ─── /ws/device challenge-response ─────────────────────
static void relayWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[RELAY] ws connected — awaiting challenge");
            break;

        case WStype_DISCONNECTED:
            relayConnected = false;
            break;

        case WStype_TEXT: {
            // Sized off the actual frame, not a fixed guess — a program_chunk
            // cmd can carry several KB of hex text, far past what a small
            // control message (challenge/authenticated/cancel/...) needs.
            // Parsing from this mutable payload buffer lets ArduinoJson
            // reference string data in place rather than copying it into the
            // document's own pool, so this isn't doubling the payload's RAM
            // cost — the +256 covers JSON structure overhead, not string data.
            DynamicJsonDocument doc(length + 256);
            if (deserializeJson(doc, payload, length)) return;
            String msgType = doc["type"] | "";

            if (msgType == "challenge") {
                String nonce = doc["nonce"] | "";
                uint8_t sig[64];
                Ed25519::sign(sig, relayPrivateKey, relayPublicKey,
                              nonce.c_str(), nonce.length());
                char sigHex[129];
                relayBytesToHex(sig, 64, sigHex);

                // 128-char signature + deviceId + object overhead — give this
                // more headroom than the smaller status-only documents above.
                DynamicJsonDocument authMsg(384);
                authMsg["type"]      = "auth";
                authMsg["deviceId"]  = relayDeviceId;
                authMsg["signature"] = sigHex;
                String out;
                serializeJson(authMsg, out);
                relayWs.sendTXT(out);
            } else if (msgType == "authenticated") {
                relayConnected = true;
                Serial.println("[RELAY] authenticated");
            } else if (msgType == "claimed") {
                // Pushed the moment someone claims this device while it's
                // connected — avoids waiting for a reboot to find out.
                relayClaimed   = true;
                relayClaimCode = "";
                Serial.println("[RELAY] claimed by account");
            } else if (msgType == "cmd") {
                relayHandleCommand(doc);
            }
            break;
        }

        default:
            break;
    }
}

void relayLoop() {
    if (!relayRegistered) return; // nothing to connect with yet

    if (!relayWsStarted) {
        relayWs.beginSslWithCA(OTABRIDGE_RELAY_HOST, OTABRIDGE_RELAY_PORT, "/ws/device",
                                OTABRIDGE_RELAY_CA_CERT);
        relayWs.onEvent(relayWsEvent);
        relayWs.setReconnectInterval(5000);
        relayWsStarted = true;
    }
    relayWs.loop();
}

// ─── Data-plane pushes ──────────────────────────────────
// Called from src/modules/02_runtime_io.inl's sseState()/sseProgress()/
// processDebugSerial() — mirrors the local SSE streams to the relay so
// there's one source of truth for "what happened," not two.
void relayPushStatus(const char* state) {
    if (!relayConnected) return;
    DynamicJsonDocument doc(128);
    doc["type"]  = "status";
    doc["state"] = state;
    String out;
    serializeJson(doc, out);
    relayWs.sendTXT(out);
}

void relayPushProgress(int page, int total, const char* label) {
    if (!relayConnected) return;
    DynamicJsonDocument doc(256);
    doc["type"]  = "progress";
    doc["page"]  = page;
    doc["total"] = total;
    doc["label"] = label;
    String out;
    serializeJson(doc, out);
    relayWs.sendTXT(out);
}

void relayPushDebugLine(const char* line) {
    if (!relayConnected) return;
    DynamicJsonDocument doc(320);
    doc["type"] = "debug_line";
    doc["line"] = line;
    String out;
    serializeJson(doc, out);
    relayWs.sendTXT(out);
}
