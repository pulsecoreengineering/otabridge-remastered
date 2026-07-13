// ─── SSE Helpers ─────────────────────────────────────
void sseLog(const char* type, const char* msg) {
    Serial.printf("[%s] %s\n", type, msg);
    String safe = String(msg);
    safe.replace("\"", "'");
    char buf[320];
    snprintf(buf, sizeof(buf), "{\"type\":\"%s\",\"msg\":\"%s\"}", type, safe.c_str());
    events.send(buf, "log", millis());
}

void sseProgress(int page, int total, const char* label) {
    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"page\":%d,\"total\":%d,\"label\":\"%s\"}", page, total, label);
    events.send(buf, "progress", millis());
    Serial.printf("[PROGRESS] %d/%d %s\n", page, total, label);
}

void sseState(const char* state) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\"}", state);
    events.send(buf, "state", millis());
    Serial.printf("[STATE] %s\n", state);
}

void sseDevice(int pidx, uint8_t sig[3]) {
    char buf[256];
    if (pidx >= 0 && pidx < profileCount) {
        DeviceProfile& p = profiles[pidx];
        snprintf(buf, sizeof(buf),
            "{\"found\":true,\"name\":\"%s\",\"mcu\":\"%s\","
            "\"sig\":\"0x%02X 0x%02X 0x%02X\","
            "\"flashSize\":%lu,\"pageSize\":%u,\"custom\":%s}",
            p.name, p.mcu, sig[0], sig[1], sig[2],
            (unsigned long)p.flashSize, p.pageSize,
            p.isCustom ? "true" : "false");
    } else {
        snprintf(buf, sizeof(buf),
            "{\"found\":false,\"sig\":\"0x%02X 0x%02X 0x%02X\"}",
            sig[0], sig[1], sig[2]);
    }
    events.send(buf, "device", millis());
}

// ─── Debug Serial Reader ─────────────────────────────
// Called from loop() when debugActive is true.
// Reads Serial2 byte-by-byte, buffers into lines, pushes
// complete lines to debugEvents SSE. Newline-terminated.
void processDebugSerial() {
    // Heartbeat every 15s to keep SSE connection alive
    if (millis() - debugLastHeartbeat > 15000) {
        debugLastHeartbeat = millis();
        debugEvents.send("", "heartbeat", millis());
    }

    if (!debugActive) return;
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            debugLineBuf.trim();
            if (debugLineBuf.length() > 0) {
                String safe = "";
                for (int i = 0; i < (int)debugLineBuf.length(); i++) {
                    char ch = debugLineBuf[i];
                    if (ch == '\\')      safe += "\\\\";
                    else if (ch == '"')  safe += "\\\"";
                    else if (ch >= 0x20) safe += ch;
                }
                char buf[320];
                snprintf(buf, sizeof(buf),
                    "{\"line\":\"%s\",\"ms\":%lu}",
                    safe.c_str(), millis());
                debugEvents.send(buf, "line", millis());
                debugLineBuf = "";
            }
        } else if (c != '\r') {
            if (debugLineBuf.length() < 256) debugLineBuf += c;
        }
    }
}

// ─── Profile Management ──────────────────────────────
void loadBuiltinProfiles() {
    for (int i = 0; i < BUILTIN_COUNT && profileCount < MAX_PROFILES; i++)
        profiles[profileCount++] = BUILTIN_PROFILES[i];
    Serial.printf("Loaded %d built-in profiles\n", BUILTIN_COUNT);
}

void loadCustomProfiles() {
    if (!LittleFS.exists("/profiles.json")) return;
    File f = LittleFS.open("/profiles.json", "r");
    if (!f) return;
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, f)) { f.close(); return; }
    f.close();
    JsonArray arr = doc.as<JsonArray>();
    int loaded = 0;
    for (JsonObject obj : arr) {
        if (profileCount >= MAX_PROFILES) break;
        DeviceProfile p;
        strlcpy(p.name,  obj["name"]  | "Custom Device", sizeof(p.name));
        strlcpy(p.mcu,   obj["mcu"]   | "Unknown",       sizeof(p.mcu));
        p.sig[0]           = obj["sig0"]       | 0;
        p.sig[1]           = obj["sig1"]       | 0;
        p.sig[2]           = obj["sig2"]       | 0;
        p.flashSize        = obj["flashSize"]  | 32768;
        p.pageSize         = obj["pageSize"]   | 128;
        p.bootloaderWaitMs = obj["bootWaitMs"] | 750;
        String proto       = obj["protocol"]   | "v1";
        p.protocol         = (proto == "v2") ? PROTO_STK500V2 : PROTO_STK500V1;
        p.isCustom         = true;
        profiles[profileCount++] = p;
        loaded++;
    }
    Serial.printf("Loaded %d custom profiles from LittleFS\n", loaded);
}

void saveCustomProfiles() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < profileCount; i++) {
        if (!profiles[i].isCustom) continue;
        JsonObject obj = arr.createNestedObject();
        obj["name"]      = profiles[i].name;
        obj["mcu"]       = profiles[i].mcu;
        obj["sig0"]      = profiles[i].sig[0];
        obj["sig1"]      = profiles[i].sig[1];
        obj["sig2"]      = profiles[i].sig[2];
        obj["flashSize"] = profiles[i].flashSize;
        obj["pageSize"]  = profiles[i].pageSize;
        obj["bootWaitMs"]= profiles[i].bootloaderWaitMs;
        obj["protocol"]  = (profiles[i].protocol == PROTO_STK500V2) ? "v2" : "v1";
    }
    File f = LittleFS.open("/profiles.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

int findProfile(uint8_t sig[3]) {
    for (int i = 0; i < profileCount; i++) {
        if (profiles[i].sig[0] == sig[0] &&
            profiles[i].sig[1] == sig[1] &&
            profiles[i].sig[2] == sig[2]) return i;
    }
    return -1;
}

