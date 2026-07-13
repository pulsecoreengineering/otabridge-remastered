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
// KNOWN MVP GAP: both the HTTPS register call and the WSS connection below
// skip TLS certificate validation (WiFiClientSecure::setInsecure(), no
// pinned fingerprint on the WebSocket client). The Ed25519 handshake itself
// still can't be forged by an on-path attacker without the private key, but
// the /devices/register response carries the claim code in the clear — an
// on-path attacker could intercept it and claim the device before its
// rightful owner does. Pin the relay's certificate/fingerprint before this
// leaves a trusted network.

static uint8_t relayPrivateKey[32];
static uint8_t relayPublicKey[32];
static WebSocketsClient relayWs;
static bool relayWsStarted = false;

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
    client.setInsecure(); // MVP — see file header note

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
            DynamicJsonDocument doc(256);
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
        relayWs.beginSSL(OTABRIDGE_RELAY_HOST, OTABRIDGE_RELAY_PORT, "/ws/device");
        relayWs.onEvent(relayWsEvent);
        relayWs.setReconnectInterval(5000);
        relayWsStarted = true;
    }
    relayWs.loop();
}
