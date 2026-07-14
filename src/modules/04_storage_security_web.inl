// ─── Config I/O ──────────────────────────────────────
void saveConfig() {
    File f = LittleFS.open("/config.json", "w");
    if (!f) return;
    DynamicJsonDocument doc(1024);
    doc["baudRate"]          = config.baudRate;
    doc["resetPin"]          = config.resetPin;
    doc["ledPin"]            = config.ledPin;
    doc["resetPulseMs"]      = config.resetPulseMs;
    doc["bootloaderWaitMs"]  = config.bootloaderWaitMs;
    doc["syncAttempts"]      = config.syncAttempts;
    doc["syncTimeoutMs"]     = config.syncTimeoutMs;
    doc["responseTimeoutMs"] = config.responseTimeoutMs;
    doc["enableVerification"]= config.enableVerification;
    serializeJson(doc, f); f.close();
    Serial.println("Config saved");
}

void loadConfig() {
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, f)) {
        config.baudRate          = doc["baudRate"]          | 115200;
        config.resetPin          = doc["resetPin"]          | 4;
        config.ledPin            = doc["ledPin"]            | 2;
        config.resetPulseMs      = doc["resetPulseMs"]      | 100;
        config.bootloaderWaitMs  = doc["bootloaderWaitMs"]  | 750;
        config.syncAttempts      = doc["syncAttempts"]      | 20;
        config.syncTimeoutMs     = doc["syncTimeoutMs"]     | 200;
        config.responseTimeoutMs = doc["responseTimeoutMs"] | 1000;
        config.enableVerification= doc["enableVerification"]| true;
        Serial.println("Config loaded");
    }
    f.close();
}

// ─── CORS ────────────────────────────────────────────
void addCORS(AsyncWebServerResponse* r) {
    // CORS is intentionally restricted to same-origin browser access.
    // Do not send wildcard Access-Control-Allow-Origin.
    r->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, PATCH, OPTIONS");
    r->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
}

bool hasValidApiKey(AsyncWebServerRequest* req) {
    // The API key is provision.deviceSecret. Provisioning guarantees a
    // secret exists before the main server ever starts, so an empty
    // secret is a fault, not a legacy device — fail closed.
    if (provision.deviceSecret.length() == 0) return false;
    if (!req->hasHeader("X-API-Key")) return false;
    return req->header("X-API-Key") == provision.deviceSecret;
}

bool requireApiKey(AsyncWebServerRequest* req) {
    if (hasValidApiKey(req)) return true;
    AsyncWebServerResponse* r = req->beginResponse(401, "application/json",
        "{\"success\":false,\"message\":\"Unauthorized (missing/invalid API key)\"}");
    addCORS(r);
    req->send(r);
    return false;
}

// ─── Web Server ──────────────────────────────────────
void setupWebServer() {

    // Apply CORS headers to EVERY response including SSE streams.
    // addHeader() on individual responses does not cover SSE —
    // DefaultHeaders is the only way to cover AsyncEventSource.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, PATCH, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");

    // ── Register ALL SSE handlers first ─────────────
    // ESPAsyncWebServer matches handlers in registration order.
    // onNotFound must come AFTER all addHandler() calls or it
    // intercepts SSE connections before they can be matched.
    events.onConnect([](AsyncEventSourceClient* c) {
        Serial.printf("SSE client connected (lastId=%llu)\n", c->lastId());
        c->send("{\"type\":\"ok\",\"msg\":\"SSE connected\"}", "log", millis());
    });
    server.addHandler(&events);

    debugEvents.onConnect([](AsyncEventSourceClient* c) {
        Serial.println("Debug SSE client connected");
        c->send("{\"line\":\"OTABridge connected\",\"ms\":0}", "line", millis());
    });
    server.addHandler(&debugEvents);

    // ── GET / — serve UI from LittleFS ───────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html",
                "<html><body style='font-family:monospace;background:#0d1117;color:#e6edf3;padding:40px'>"
                "<h2>⬡ OTABridge</h2>"
                "<p>UI not found — upload <code>index.html</code> to device storage.</p>"
                "<p>Run <code>pio run --target uploadfs</code> to upload the filesystem image.</p>"
                "</body></html>");
        }
    });

    // onNotFound AFTER all handlers
    server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse* r = req->beginResponse(200);
            addCORS(r); req->send(r);
        } else {
            AsyncWebServerResponse* r = req->beginResponse(404,
                "application/json", "{\"error\":\"Not found\"}");
            addCORS(r); req->send(r);
        }
    });

    // GET /api/status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        const char* s = "idle";
        switch(programmerState) {
            case STATE_IDLE:              s="idle";             break;
            case STATE_LOADING_HEX:       s="loading";          break;
            case STATE_ENTERING_PROGMODE: s="entering_progmode";break;
            case STATE_READING_SIGNATURE: s="reading_signature";break;
            case STATE_AWAITING_OVERRIDE: s="awaiting_override";break;
            case STATE_PROGRAMMING:       s="programming";      break;
            case STATE_EXITING_PROGMODE:  s="exiting";          break;
            case STATE_SUCCESS:           s="success";          break;
            case STATE_ERROR:             s="error";            break;
        }
        DynamicJsonDocument doc(512);
        doc["state"]      = s;
        doc["busy"]       = (programmerState != STATE_IDLE &&
                             programmerState != STATE_SUCCESS &&
                             programmerState != STATE_ERROR &&
                             programmerState != STATE_AWAITING_OVERRIDE);
        doc["hexLoaded"]  = hexIngestSize() > 0;
        doc["page"]       = currentPage;
        doc["totalPages"] = totalPages;
        doc["lastError"]  = lastError;
        char sigStr[24];
        snprintf(sigStr, sizeof(sigStr), "0x%02X 0x%02X 0x%02X",
                 detectedSig[0], detectedSig[1], detectedSig[2]);
        doc["detectedSig"] = sigStr;
        doc["detectedProtocol"] = detectedProtocol == PROTO_STK500V2 ? "STK500v2" :
                                  detectedProtocol == PROTO_STK500V1 ? "STK500v1" : "unknown";
        if (activeProfileIndex >= 0) {
            doc["detectedDevice"] = profiles[activeProfileIndex].name;
            doc["detectedMCU"]    = profiles[activeProfileIndex].mcu;
        }
        String out; serializeJson(doc, out);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", out);
        addCORS(r); req->send(r);
    });

    // GET /api/info
    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* req) {
        DynamicJsonDocument doc(512);
        doc["deviceName"]   = provision.deviceName;
        doc["version"]      = OTABRIDGE_FW_VERSION;
        doc["chipModel"]    = ESP.getChipModel();
        doc["chipRevision"] = ESP.getChipRevision();
        doc["cpuFreq"]      = ESP.getCpuFreqMHz();
        doc["flashSize"]    = ESP.getFlashChipSize();
        doc["freeHeap"]     = ESP.getFreeHeap();
        doc["wifiMode"]     = (WiFi.getMode()==WIFI_AP) ? "Access Point" : "Station";
        doc["ipAddress"]    = (WiFi.getMode()==WIFI_AP)
                              ? WiFi.softAPIP().toString()
                              : WiFi.localIP().toString();
        doc["macAddress"]   = WiFi.macAddress();
        String out; serializeJson(doc, out);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", out);
        addCORS(r); req->send(r);
    });

    // GET /api/relay/status — read-only, so the claim code stays visible
    // beyond the boot-time serial log (a fresh code needs a reboot once the
    // 15-minute window lapses; see src/modules/06_relay_client.inl).
    server.on("/api/relay/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        DynamicJsonDocument doc(256);
        doc["registered"] = relayRegistered;
        doc["deviceId"]   = relayDeviceId;
        doc["claimed"]    = relayClaimed;
        doc["connected"]  = relayConnected;
        if (!relayClaimed) doc["claimCode"] = relayClaimCode;
        String out; serializeJson(doc, out);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", out);
        addCORS(r); req->send(r);
    });

    // GET /api/profiles
    server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest* req) {
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < profileCount; i++) {
            JsonObject obj = arr.createNestedObject();
            obj["index"]     = i;
            obj["name"]      = profiles[i].name;
            obj["mcu"]       = profiles[i].mcu;
            char sig[24];
            snprintf(sig, sizeof(sig), "0x%02X 0x%02X 0x%02X",
                     profiles[i].sig[0], profiles[i].sig[1], profiles[i].sig[2]);
            obj["signature"] = sig;
            obj["flashSize"] = profiles[i].flashSize;
            obj["pageSize"]  = profiles[i].pageSize;
            obj["bootWaitMs"]= profiles[i].bootloaderWaitMs;
            obj["custom"]    = profiles[i].isCustom;
        }
        String out; serializeJson(doc, out);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", out);
        addCORS(r); req->send(r);
    });

    // POST /api/profiles
    server.on("/api/profiles", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            if (!requireApiKey(req)) return;
            if (profileCount >= MAX_PROFILES) {
                AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                    "{\"success\":false,\"message\":\"Max profiles reached\"}");
                addCORS(r); req->send(r); return;
            }
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, data, len)) {
                AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
                addCORS(r); req->send(r); return;
            }
            DeviceProfile p;
            strlcpy(p.name, doc["name"] | "Custom Device", sizeof(p.name));
            strlcpy(p.mcu,  doc["mcu"]  | "Unknown",       sizeof(p.mcu));
            p.sig[0]           = doc["sig0"]       | 0;
            p.sig[1]           = doc["sig1"]       | 0;
            p.sig[2]           = doc["sig2"]       | 0;
            p.flashSize        = doc["flashSize"]  | 32768;
            p.pageSize         = doc["pageSize"]   | 128;
            p.bootloaderWaitMs = doc["bootWaitMs"] | 750;
            p.isCustom         = true;
            profiles[profileCount++] = p;
            saveCustomProfiles();
            AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
                "{\"success\":true}");
            addCORS(r); req->send(r);
        }
    );

    // DELETE /api/profiles?index=N
    server.on("/api/profiles", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        if (!req->hasParam("index")) {
            AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                "{\"success\":false,\"message\":\"index required\"}");
            addCORS(r); req->send(r); return;
        }
        int idx = req->getParam("index")->value().toInt();
        if (idx < 0 || idx >= profileCount || !profiles[idx].isCustom) {
            AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                "{\"success\":false,\"message\":\"Invalid index or built-in\"}");
            addCORS(r); req->send(r); return;
        }
        for (int i = idx; i < profileCount-1; i++) profiles[i] = profiles[i+1];
        profileCount--;
        saveCustomProfiles();
        AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
            "{\"success\":true}");
        addCORS(r); req->send(r);
    });

    // GET /api/config
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        DynamicJsonDocument doc(512);
        doc["baudRate"]          = config.baudRate;
        doc["resetPin"]          = config.resetPin;
        doc["ledPin"]            = config.ledPin;
        doc["resetPulseMs"]      = config.resetPulseMs;
        doc["bootloaderWaitMs"]  = config.bootloaderWaitMs;
        doc["syncAttempts"]      = config.syncAttempts;
        doc["syncTimeoutMs"]     = config.syncTimeoutMs;
        doc["responseTimeoutMs"] = config.responseTimeoutMs;
        doc["enableVerification"]= config.enableVerification;
        doc["wifiSSID"]          = provision.wifiSSID;   // read-only view of provision
        String out; serializeJson(doc, out);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", out);
        addCORS(r); req->send(r);
    });

    // POST /api/config
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            if (!requireApiKey(req)) return;
            DynamicJsonDocument doc(1024);
            if (deserializeJson(doc, data, len)) {
                AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
                addCORS(r); req->send(r); return;
            }
            config.baudRate          = doc["baudRate"]          | config.baudRate;
            config.resetPin          = doc["resetPin"]          | config.resetPin;
            config.ledPin            = doc["ledPin"]            | config.ledPin;
            config.resetPulseMs      = doc["resetPulseMs"]      | config.resetPulseMs;
            config.bootloaderWaitMs  = doc["bootloaderWaitMs"]  | config.bootloaderWaitMs;
            config.syncAttempts      = doc["syncAttempts"]      | config.syncAttempts;
            config.syncTimeoutMs     = doc["syncTimeoutMs"]     | config.syncTimeoutMs;
            config.responseTimeoutMs = doc["responseTimeoutMs"] | config.responseTimeoutMs;
            config.enableVerification= doc["enableVerification"]| config.enableVerification;
            // WiFi credential changes are routed to the provision record —
            // single source of truth. Takes effect after restart.
            {
                bool dirty = false;
                String newSSID = doc["wifiSSID"] | "";
                String newPass = doc["wifiPassword"] | "";
                if (newSSID.length() > 0 && newSSID != provision.wifiSSID) {
                    provision.wifiSSID = newSSID; dirty = true;
                }
                if (newPass.length() > 0) {
                    provision.wifiPassword = newPass; dirty = true;
                }
                if (dirty) saveProvision();
            }
            pinMode(config.ledPin, OUTPUT);
            restartProgrammers(config.baudRate);
            saveConfig();
            AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
                "{\"success\":true}");
            addCORS(r); req->send(r);
        }
    );

    // POST /api/config/reset
    server.on("/api/config/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        config = ProgrammerConfig();
        pinMode(config.ledPin, OUTPUT);
        restartProgrammers(config.baudRate);
        saveConfig();
        AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
            "{\"success\":true}");
        addCORS(r); req->send(r);
    });

    // POST /api/upload-hex
    server.on("/api/upload-hex", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!hasValidApiKey(req)) {
                if (index == 0) requireApiKey(req);
                return;
            }
            if (programmerState != STATE_IDLE &&
                programmerState != STATE_SUCCESS &&
                programmerState != STATE_ERROR) {
                AsyncWebServerResponse* r = req->beginResponse(409,"application/json",
                    "{\"success\":false,\"message\":\"Programmer busy\"}");
                addCORS(r); req->send(r); return;
            }
            if (index == 0) {
                activeProfileIndex = -1;
                manualOverride.active = false;
                if (!hexIngestBegin()) {
                    AsyncWebServerResponse* r = req->beginResponse(500,"application/json",
                        "{\"success\":false,\"message\":\"Failed to open storage for hex data\"}");
                    addCORS(r); req->send(r); return;
                }
            }
            if (!hexIngestAppend(data, len)) {
                hexIngestEnd();
                hexIngestClear();
                AsyncWebServerResponse* r = req->beginResponse(500,"application/json",
                    "{\"success\":false,\"message\":\"Storage write failed (out of space?)\"}");
                addCORS(r); req->send(r); return;
            }
            if (index + len == total) {
                hexIngestEnd();
                programmerState = STATE_IDLE; lastError = "";
                char buf[80];
                snprintf(buf, sizeof(buf),
                    "{\"success\":true,\"size\":%d}", (int)hexIngestSize());
                AsyncWebServerResponse* r = req->beginResponse(200,"application/json", buf);
                addCORS(r); req->send(r);
                Serial.printf("HEX received: %d bytes\n", (int)hexIngestSize());
            }
        }
    );

    // POST /api/program
    server.on("/api/program", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        if (programmerState != STATE_IDLE &&
            programmerState != STATE_SUCCESS &&
            programmerState != STATE_ERROR) {
            AsyncWebServerResponse* r = req->beginResponse(409,"application/json",
                "{\"success\":false,\"message\":\"Already programming\"}");
            addCORS(r); req->send(r); return;
        }
        if (hexIngestSize() == 0) {
            AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                "{\"success\":false,\"message\":\"No hex data loaded\"}");
            addCORS(r); req->send(r); return;
        }
        manualOverride.active = false;
        activeProfileIndex    = -1;
        programmerState       = STATE_LOADING_HEX;
        lastError             = "";
        cancelFlashRequested  = false;
        // Protocol preference from UI dropdown
        if (req->hasParam("protocol")) {
            String p = req->getParam("protocol")->value();
            if      (p == "v1") preferredProtocol = PROTO_STK500V1;
            else if (p == "v2") preferredProtocol = PROTO_STK500V2;
            else                preferredProtocol = PROTO_UNKNOWN;  // auto
        } else {
            preferredProtocol = PROTO_UNKNOWN;  // auto
        }
        // Core 0, not core 1 — loop() (which pumps AsyncTCP locally, and
        // WebSocketsClient's relay connection) runs on core 1 by default.
        // Sharing that core with this task starves whichever of those needs
        // polling, most visibly the relay path (see relay client's own note).
        xTaskCreatePinnedToCore(
            flashTask, "flashTask", 8192, NULL, 1, &flashTaskHandle, 0);
        AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
            "{\"success\":true,\"message\":\"Flash task started\"}");
        addCORS(r); req->send(r);
    });

    // POST /api/override
    server.on("/api/override", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            if (!requireApiKey(req)) return;
            if (programmerState != STATE_IDLE &&
                programmerState != STATE_SUCCESS &&
                programmerState != STATE_ERROR &&
                programmerState != STATE_AWAITING_OVERRIDE) {
                AsyncWebServerResponse* r = req->beginResponse(409,"application/json",
                    "{\"success\":false,\"message\":\"Programmer busy\"}");
                addCORS(r); req->send(r); return;
            }
            if (hexIngestSize() == 0) {
                AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                    "{\"success\":false,\"message\":\"No hex data loaded\"}");
                addCORS(r); req->send(r); return;
            }
            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, data, len)) {
                AsyncWebServerResponse* r = req->beginResponse(400,"application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
                addCORS(r); req->send(r); return;
            }
            manualOverride.active    = true;
            manualOverride.flashSize = doc["flashSize"] | 32768;
            manualOverride.pageSize  = doc["pageSize"]  | 128;
            // protocol: "v1" or "v2", defaults to v1
            String proto = doc["protocol"] | "v1";
            manualOverride.protocol = (proto == "v2") ? PROTO_STK500V2 : PROTO_STK500V1;
            programmerState = STATE_LOADING_HEX; lastError = "";
            cancelFlashRequested = false;
            // Core 0 — see the note on the /api/program task creation above.
            xTaskCreatePinnedToCore(
                flashTask, "flashTask", 8192, NULL, 1, &flashTaskHandle, 0);
            AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
                "{\"success\":true}");
            addCORS(r); req->send(r);
        }
    );

    // POST /api/cancel
    server.on("/api/cancel", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        manualOverride.active = false;
        activeProfileIndex    = -1;
        if (flashTaskHandle != NULL) {
            cancelFlashRequested = true;
            sseLog("warn", "Cancellation requested");
            AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
                "{\"success\":true,\"message\":\"Cancelling active flash task\"}");
            addCORS(r); req->send(r); return;
        }
        programmerState = STATE_IDLE;
        lastError = "Cancelled by user";
        sseState("idle");
        AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
            "{\"success\":true,\"message\":\"No active flash task\"}");
        addCORS(r); req->send(r);
    });

    // POST /api/restart
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        AsyncWebServerResponse* r = req->beginResponse(200,"application/json",
            "{\"success\":true}");
        addCORS(r); req->send(r);
        delay(500); ESP.restart();
    });

    // GET /api/debug/status
    server.on("/api/debug/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        char buf[80];
        snprintf(buf, sizeof(buf),
            "{\"active\":%s,\"baud\":%lu}",
            debugActive ? "true" : "false",
            (unsigned long)debugBaudRate);
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json", buf);
        addCORS(r); req->send(r);
    });

    // POST /api/debug/start  — body: {"baud":115200}
    server.on("/api/debug/start", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            if (!requireApiKey(req)) return;
            if (programmerState != STATE_IDLE &&
                programmerState != STATE_SUCCESS &&
                programmerState != STATE_ERROR) {
                AsyncWebServerResponse* r = req->beginResponse(409, "application/json",
                    "{\"success\":false,\"message\":\"Cannot start debug while programming\"}");
                addCORS(r); req->send(r); return;
            }
            DynamicJsonDocument doc(128);
            if (!deserializeJson(doc, data, len))
                debugBaudRate = doc["baud"] | 9600;
            Serial1.end();
            delay(50);
            Serial1.begin(debugBaudRate, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
            delay(50);
            while (Serial1.available()) Serial1.read(); // flush
            debugLineBuf = "";
            debugActive  = true;
            Serial.printf("Debug monitor started @ %lu baud on GPIO%d/GPIO%d\n",
                (unsigned long)debugBaudRate, DEBUG_RX_PIN, DEBUG_TX_PIN);
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json",
                "{\"success\":true}");
            addCORS(r); req->send(r);
        }
    );

    // POST /api/debug/stop
    server.on("/api/debug/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!requireApiKey(req)) return;
        debugActive = false;
        debugLineBuf = "";
        Serial1.end();
        Serial.println("Debug monitor stopped");
        AsyncWebServerResponse* r = req->beginResponse(200, "application/json",
            "{\"success\":true}");
        addCORS(r); req->send(r);
    });

    // POST /api/debug/send  — body: {"cmd":"your command\n"}
    server.on("/api/debug/send", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            if (!requireApiKey(req)) return;
            if (!debugActive) {
                AsyncWebServerResponse* r = req->beginResponse(400, "application/json",
                    "{\"success\":false,\"message\":\"Debug not active\"}");
                addCORS(r); req->send(r); return;
            }
            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, data, len)) {
                AsyncWebServerResponse* r = req->beginResponse(400, "application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
                addCORS(r); req->send(r); return;
            }
            String cmd = doc["cmd"] | "";
            cmd.trim();
            if (cmd.length() > 0) {
                Serial1.print(cmd);
                Serial1.print('\n');  // Mega needs \n to terminate readStringUntil('\n')
                // Echo sent command back to debug console
                cmd.replace("\\", "\\\\");
                cmd.replace("\"", "\\\"");
                cmd.trim();
                char buf[320];
                snprintf(buf, sizeof(buf),
                    "{\"line\":\"> %s\",\"ms\":%lu,\"sent\":true}",
                    cmd.c_str(), millis());
                debugEvents.send(buf, "line", millis());
            }
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json",
                "{\"success\":true}");
            addCORS(r); req->send(r);
        }
    );


    server.begin();
    Serial.println("Web server ready on port 80");
}

