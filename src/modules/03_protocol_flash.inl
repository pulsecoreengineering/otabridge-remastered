// ─── Intel HEX Parser ────────────────────────────────
// Kept exactly as in the working code — String by value
// so &line[n] is valid (non-const lvalue).
struct HexRecord {
    uint8_t  length;
    uint16_t address;
    uint8_t  type;
    uint8_t  data[256];
    uint8_t  checksum;
    bool     valid;
};

class IntelHexParser {
private:
    uint32_t extendedAddress = 0;

    uint8_t hexCharToByte(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    }

    uint8_t hexStringToByte(const char* hex) {
        return (hexCharToByte(hex[0]) << 4) | hexCharToByte(hex[1]);
    }

public:
    // String passed by VALUE — same as working code, avoids lvalue error
    HexRecord parseLine(String line) {
        HexRecord record = {0};
        if (line.length() < 11 || line[0] != ':') {
            record.valid = false;
            return record;
        }
        record.length  = hexStringToByte(&line[1]);
        record.address = (hexStringToByte(&line[3]) << 8) |
                          hexStringToByte(&line[5]);
        record.type    = hexStringToByte(&line[7]);
        for (int i = 0; i < record.length; i++) {
            if (9 + i*2 + 1 >= (int)line.length()) {
                record.valid = false;
                return record;
            }
            record.data[i] = hexStringToByte(&line[9 + i*2]);
        }
        record.checksum = hexStringToByte(&line[9 + record.length*2]);
        uint8_t calc = record.length + (record.address >> 8) +
                       (record.address & 0xFF) + record.type;
        for (int i = 0; i < record.length; i++) calc += record.data[i];
        calc = (~calc + 1);
        record.valid = (calc == record.checksum);
        if (record.valid && record.type == 0x04)
            extendedAddress = ((uint32_t)record.data[0] << 24) |
                              ((uint32_t)record.data[1] << 16);
        return record;
    }

    uint32_t getAbsoluteAddress(HexRecord& record) {
        return extendedAddress + record.address;
    }

    void reset() { extendedAddress = 0; }
};

// ─── STK500 Programmer ───────────────────────────────
// Protocol layer kept exactly as working code.
// Only change: Serial.print → sseLog so output goes to UI.
class STK500Programmer {
private:
    HardwareSerial* serial;
    int resetPin;

    bool waitForResponse(uint8_t expected, int timeout = 1000) {
        unsigned long start = millis();
        while (millis() - start < (unsigned long)timeout) {
            if (serial->available()) {
                uint8_t response = serial->read();
                if (response == expected) return true;
            }
            delay(1);
        }
        return false;
    }

    void flushSerial() {
        while (serial->available()) serial->read();
    }

public:
    STK500Programmer(HardwareSerial* ser, int reset)
        : serial(ser), resetPin(reset) {}

    void begin(uint32_t baud) {
        serial->begin(baud, SERIAL_8N1, 16, 17);
    }

    void restart(uint32_t baud) {
        serial->end();
        serial->begin(baud, SERIAL_8N1, 16, 17);
    }

    // bootWaitMs overridable by device profile
    bool enterProgramMode(uint32_t bootWaitMs = 750) {
        sseLog("info", "Resetting target MCU...");

        // Exact reset sequence from working code:
        // pulse LOW, then release to INPUT so internal pullup floats it
        pinMode(resetPin, OUTPUT);
        digitalWrite(resetPin, LOW);
        delay(config.resetPulseMs);
        pinMode(resetPin, INPUT);   // <-- critical, not OUTPUT HIGH
        delay(bootWaitMs);

        flushSerial();

        char buf[48];
        for (int attempts = 0; attempts < config.syncAttempts; attempts++) {
            snprintf(buf, sizeof(buf), "Sync attempt %d / %d",
                     attempts + 1, config.syncAttempts);
            sseLog("dim", buf);

            serial->write(STK_GET_SYNC);
            serial->write(CRC_EOP);

            // 200ms per attempt — same as working code
            if (waitForResponse(STK_INSYNC, config.syncTimeoutMs) &&
                waitForResponse(STK_OK,     config.syncTimeoutMs)) {
                sseLog("ok", "Bootloader sync established");
                return true;
            }
            delay(100);
        }
        sseLog("err", "Sync failed — check wiring and baud rate");
        return false;
    }

    bool getSignature(uint8_t signature[3]) {
        sseLog("info", "Reading device signature...");
        flushSerial();
        serial->write(STK_READ_SIGN);
        serial->write(CRC_EOP);

        if (!waitForResponse(STK_INSYNC, config.responseTimeoutMs)) {
            sseLog("err", "No INSYNC for signature");
            return false;
        }
        for (int i = 0; i < 3; i++) {
            unsigned long start = millis();
            while (!serial->available() &&
                   millis() - start < (unsigned long)config.responseTimeoutMs);
            if (!serial->available()) {
                sseLog("err", "Timeout reading signature");
                return false;
            }
            signature[i] = serial->read();
        }
        if (!waitForResponse(STK_OK, config.responseTimeoutMs)) {
            sseLog("err", "No OK after signature");
            return false;
        }
        char buf[56];
        snprintf(buf, sizeof(buf),
            "Signature: 0x%02X 0x%02X 0x%02X",
            signature[0], signature[1], signature[2]);
        sseLog("dim", buf);
        return true;
    }

    bool loadAddress(uint16_t address) {
        uint16_t wordAddress = address / 2;
        flushSerial();
        serial->write(STK_LOAD_ADDRESS);
        serial->write(wordAddress & 0xFF);
        serial->write((wordAddress >> 8) & 0xFF);
        serial->write(CRC_EOP);
        return waitForResponse(STK_INSYNC, config.responseTimeoutMs) &&
               waitForResponse(STK_OK,     config.responseTimeoutMs);
    }

    bool programPage(uint8_t* data, uint16_t size) {
        flushSerial();
        serial->write(STK_PROG_PAGE);
        serial->write((size >> 8) & 0xFF);
        serial->write(size & 0xFF);
        serial->write('F');
        for (uint16_t i = 0; i < size; i++) serial->write(data[i]);
        serial->write(CRC_EOP);
        return waitForResponse(STK_INSYNC, 10000) &&
               waitForResponse(STK_OK,     10000);
    }

    bool verifyPage(uint8_t* expected, uint16_t address, uint16_t size) {
        if (!loadAddress(address)) return false;
        flushSerial();
        serial->write(STK_READ_PAGE);
        serial->write((size >> 8) & 0xFF);
        serial->write(size & 0xFF);
        serial->write('F');
        serial->write(CRC_EOP);
        if (!waitForResponse(STK_INSYNC, config.responseTimeoutMs)) return false;
        for (uint16_t i = 0; i < size; i++) {
            unsigned long start = millis();
            while (!serial->available() &&
                   millis() - start < (unsigned long)config.responseTimeoutMs);
            if (!serial->available()) return false;
            uint8_t readByte = serial->read();
            if (readByte != expected[i]) {
                char buf[80];
                snprintf(buf, sizeof(buf),
                    "Verify failed @ 0x%04X: expected 0x%02X got 0x%02X",
                    address + i, expected[i], readByte);
                sseLog("err", buf);
                return false;
            }
        }
        return waitForResponse(STK_OK, config.responseTimeoutMs);
    }

    void exitProgramMode() {
        sseLog("info", "Exiting program mode...");
        serial->write(STK_LEAVE_PROGMODE);
        serial->write(CRC_EOP);
        delay(100);
    }
};

// ─── STK500v2 Programmer ─────────────────────────────
// Used for ATmega2560, ATmega1280.
// Packet format: 0x1B | SEQ | SIZE_H | SIZE_L | 0x0E | CMD... | XOR_CHECKSUM
class STK500v2Programmer {
private:
    HardwareSerial* serial;
    int             resetPin;
    uint8_t         seqNum = 0;

    uint8_t xorChecksum(uint8_t* buf, int len) {
        uint8_t cs = 0;
        for (int i = 0; i < len; i++) cs ^= buf[i];
        return cs;
    }

    void flushSerial() {
        unsigned long t = millis();
        while (millis() - t < 20) {
            while (serial->available()) { serial->read(); t = millis(); }
            delay(1);
        }
    }

    // Send a STK500v2 packet
    void sendPacket(uint8_t* body, uint16_t bodyLen) {
        uint8_t header[5];
        header[0] = 0x1B;
        header[1] = seqNum++;
        header[2] = (bodyLen >> 8) & 0xFF;
        header[3] = bodyLen & 0xFF;
        header[4] = 0x0E;
        serial->write(header, 5);
        serial->write(body, bodyLen);
        // Checksum over entire packet (header + body)
        uint8_t cs = 0;
        for (int i = 0; i < 5;       i++) cs ^= header[i];
        for (int i = 0; i < bodyLen; i++) cs ^= body[i];
        serial->write(cs);
    }

    // Receive a STK500v2 response packet into buf, returns body length or -1
    int recvPacket(uint8_t* buf, int maxLen, uint32_t timeoutMs = 2000) {
        unsigned long start = millis();
        // Wait for MESSAGE_START 0x1B
        while (millis() - start < timeoutMs) {
            if (serial->available()) {
                if (serial->read() == 0x1B) break;
            }
            delay(1);
        }
        if (millis() - start >= timeoutMs) return -1;

        // Read rest of header: seq, size_h, size_l, token
        uint8_t hdr[4];
        for (int i = 0; i < 4; i++) {
            start = millis();
            while (!serial->available() && millis() - start < timeoutMs) delay(1);
            if (!serial->available()) return -1;
            hdr[i] = serial->read();
        }
        if (hdr[3] != 0x0E) return -1;  // token mismatch

        uint16_t bodyLen = ((uint16_t)hdr[1] << 8) | hdr[2];
        if (bodyLen > (uint16_t)maxLen) return -1;

        for (int i = 0; i < bodyLen; i++) {
            start = millis();
            while (!serial->available() && millis() - start < timeoutMs) delay(1);
            if (!serial->available()) return -1;
            buf[i] = serial->read();
        }

        // Read and verify checksum
        start = millis();
        while (!serial->available() && millis() - start < timeoutMs) delay(1);
        if (!serial->available()) return -1;
        uint8_t rxCs = serial->read();

        uint8_t cs = 0x1B;
        for (int i = 0; i < 4;       i++) cs ^= hdr[i];
        for (int i = 0; i < bodyLen; i++) cs ^= buf[i];
        if (cs != rxCs) {
            sseLog("err", "STK500v2 checksum mismatch");
            return -1;
        }
        return bodyLen;
    }

public:
    STK500v2Programmer(HardwareSerial* ser, int reset)
        : serial(ser), resetPin(reset), seqNum(0) {}

    bool enterProgramMode(uint32_t bootWaitMs = 750) {
        sseLog("info", "STK500v2: Resetting target...");
        seqNum = 0;

        pinMode(resetPin, OUTPUT);
        digitalWrite(resetPin, LOW);
        delay(config.resetPulseMs);
        pinMode(resetPin, INPUT);
        delay(bootWaitMs);
        flushSerial();

        // CMD_SIGN_ON (0x01)
        for (int attempt = 0; attempt < config.syncAttempts; attempt++) {
            char buf[48];
            snprintf(buf, sizeof(buf), "v2 sign-on attempt %d / %d",
                     attempt + 1, config.syncAttempts);
            sseLog("dim", buf);

            uint8_t cmd[1] = { 0x01 };
            sendPacket(cmd, 1);

            uint8_t resp[32];
            int len = recvPacket(resp, sizeof(resp), config.syncTimeoutMs * 2);
            if (len >= 2 && resp[0] == 0x01 && resp[1] == 0x00) {
                sseLog("ok", "STK500v2 sign-on OK");

                // CMD_ENTER_PROGMODE_ISP (0x10)
                // Parameters tuned for ATmega2560 / STK500v2 bootloader.
                // These match what avrdude sends for the Mega 2560.
                uint8_t enter[12] = {
                    0x10,   // CMD_ENTER_PROGMODE_ISP
                    200,    // timeout (ms)
                    100,    // stabDelay (ms) — stabilisation after reset
                    25,     // cmdexeDelay (ms)
                    32,     // synchLoops
                    0,      // byteDelay
                    0x53,   // pollValue (response byte to watch)
                    0x04,   // pollIndex — byte 4 of SPI response (Mega needs 4, not 3)
                    0xAC, 0x53, 0x00, 0x00  // SPI programming enable command
                };
                sendPacket(enter, sizeof(enter));
                int elen = recvPacket(resp, sizeof(resp), config.responseTimeoutMs);
                if (elen >= 2 && resp[0] == 0x10 && resp[1] == 0x00) {
                    sseLog("ok", "STK500v2 entered program mode");
                    return true;
                }
                sseLog("err", "STK500v2 enter prog mode failed");
                return false;
            }
            delay(100);
        }
        sseLog("err", "STK500v2 sign-on failed — is this a Mega?");
        return false;
    }

    bool getSignature(uint8_t signature[3]) {
        sseLog("info", "STK500v2: Reading signature...");
        // CMD_READ_SIGNATURE_ISP (0x1B)
        // Body: [0x1B][numTx=4][retAddr][spi0=0x30][spi1=0x00][spi2=addr][spi3=0x00]
        // retAddr: which byte of SPI response to return.
        // ATmega bootloaders use retAddr=4 (1-indexed = last of 4 bytes).
        // avrdude uses retAddr=4 for signature reads.
        for (int i = 0; i < 3; i++) {
            uint8_t cmd[7] = {
                0x1B,        // CMD_READ_SIGNATURE_ISP
                4,           // numTx: 4 SPI bytes
                4,           // retAddr: return 4th SPI byte (1-indexed) — where sig lives
                0x30,        // SPI[0]: read signature opcode
                0x00,        // SPI[1]: always 0
                (uint8_t)i,  // SPI[2]: byte address (0=manuf, 1=family, 2=device)
                0x00         // SPI[3]: dummy — signature returned here
            };
            sendPacket(cmd, 7);
            uint8_t resp[8];
            int len = recvPacket(resp, sizeof(resp), config.responseTimeoutMs);
            // Log raw response for debugging
            if (len >= 3) {
                char dbuf[64];
                snprintf(dbuf, sizeof(dbuf),
                    "sig[%d] raw: len=%d [0x%02X 0x%02X 0x%02X]",
                    i, len, resp[0], resp[1], resp[2]);
                sseLog("dim", dbuf);
            }
            if (len < 3 || resp[0] != 0x1B || resp[1] != 0x00) {
                char ebuf[64];
                snprintf(ebuf, sizeof(ebuf),
                    "STK500v2 sig byte %d failed (len=%d resp=0x%02X 0x%02X)",
                    i, len, len>0?resp[0]:0, len>1?resp[1]:0);
                sseLog("err", ebuf);
                return false;
            }
            signature[i] = resp[2];
        }
        char buf[56];
        snprintf(buf, sizeof(buf),
            "Signature: 0x%02X 0x%02X 0x%02X",
            signature[0], signature[1], signature[2]);
        sseLog("dim", buf);
        return true;
    }

    bool loadAddress(uint32_t byteAddress) {
        // CMD_LOAD_ADDRESS (0x06)
        // STK500v2 spec: address is a 32-bit WORD address (byte addr / 2)
        // bit 31 = 0 for flash, 1 for EEPROM
        uint32_t wordAddr = byteAddress / 2;
        uint8_t cmd[5] = {
            0x06,
            (uint8_t)((wordAddr >> 24) & 0xFF),
            (uint8_t)((wordAddr >> 16) & 0xFF),
            (uint8_t)((wordAddr >> 8)  & 0xFF),
            (uint8_t)((wordAddr)       & 0xFF)
        };
        sendPacket(cmd, 5);
        uint8_t resp[4];
        int len = recvPacket(resp, sizeof(resp), config.responseTimeoutMs);
        return len >= 2 && resp[0] == 0x06 && resp[1] == 0x00;
    }

    bool programPage(uint8_t* data, uint16_t size, uint32_t byteAddress) {
        if (!loadAddress(byteAddress)) return false;

        // CMD_PROGRAM_FLASH_ISP (0x13)
        uint8_t* pkt = (uint8_t*)malloc(10 + size);
        if (!pkt) return false;
        pkt[0] = 0x13;
        pkt[1] = (size >> 8) & 0xFF;
        pkt[2] = size & 0xFF;
        pkt[3] = 0xC1;   // mode: page write + load addr auto-advance
        pkt[4] = 10;     // delay ms
        pkt[5] = 0x40;   // cmd1: load flash page byte
        pkt[6] = 0x4C;   // cmd2: write flash page
        pkt[7] = 0x20;   // cmd3: poll RDY/BSY
        pkt[8] = 0x00;   // poll value 1
        pkt[9] = 0x00;   // poll value 2
        memcpy(pkt + 10, data, size);
        sendPacket(pkt, 10 + size);
        free(pkt);

        uint8_t resp[4];
        int len = recvPacket(resp, sizeof(resp), 10000);
        return len >= 2 && resp[0] == 0x13 && resp[1] == 0x00;
    }

    bool verifyPage(uint8_t* expected, uint32_t byteAddress, uint16_t size) {
        // Must reload address — bootloader pointer advanced after programPage
        if (!loadAddress(byteAddress)) return false;

        // CMD_READ_FLASH_ISP (0x14)
        uint8_t cmd[4] = {
            0x14,
            (uint8_t)((size >> 8) & 0xFF),
            (uint8_t)(size & 0xFF),
            0x20   // memory type: flash
        };
        sendPacket(cmd, 4);

        // Response: [0x14, status, data[size], 0x14]  — allow generous timeout
        // for large pages (256B) on slow bootloaders
        uint32_t timeoutMs = max((uint32_t)5000,
                                 (uint32_t)config.responseTimeoutMs * 5);
        uint8_t* resp = (uint8_t*)malloc(size + 8);
        if (!resp) return false;
        int len = recvPacket(resp, size + 8, timeoutMs);
        if (len < (int)(size + 2) || resp[0] != 0x14 || resp[1] != 0x00) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                "v2 read-back failed @ 0x%05lX (len=%d status=0x%02X)",
                (unsigned long)byteAddress, len, len >= 2 ? resp[1] : 0xFF);
            sseLog("err", buf);
            free(resp); return false;
        }
        for (uint16_t i = 0; i < size; i++) {
            if (resp[2 + i] != expected[i]) {
                char buf[80];
                snprintf(buf, sizeof(buf),
                    "v2 verify fail @ 0x%05lX: exp 0x%02X got 0x%02X",
                    (unsigned long)(byteAddress + i), expected[i], resp[2 + i]);
                sseLog("err", buf);
                free(resp); return false;
            }
        }
        free(resp);
        return true;
    }

    void exitProgramMode() {
        sseLog("info", "STK500v2: Leaving program mode...");
        // CMD_LEAVE_PROGMODE_ISP (0x11)
        uint8_t cmd[3] = { 0x11, 1, 1 };
        sendPacket(cmd, 3);
        uint8_t resp[4];
        recvPacket(resp, sizeof(resp), 500);
        delay(100);
    }
};

// ─── Globals ─────────────────────────────────────────
// Defined here — AFTER both class definitions so the types are known.
STK500Programmer*   programmer       = nullptr;
STK500v2Programmer* programmerV2     = nullptr;
IntelHexParser      parser;
FlashProtocol       detectedProtocol  = PROTO_UNKNOWN;
FlashProtocol       preferredProtocol = PROTO_UNKNOWN;  // UNKNOWN = auto

// ─── Flash Task ──────────────────────────────────────
// ── flashTask ────────────────────────────────────────
// Streaming design: parse HEX into a per-page bitmap (pageHasData)
// on the first pass, then on the second pass fill one page-sized buffer
// and flash it immediately — reusing the same buffer for every page.
// Peak heap: pageSize (256B max) + pageHasData bitmap (128B for Mega).
// No large contiguous allocation — works for both Uno (32KB) and Mega (256KB).

void flashTask(void* param) {
    char buf[128];
    bool useV2 = false;
    bool enteredProgramMode = false;

    // Resolve flash/page/bootwait — may be updated after signature read in Stage 3
    uint32_t flashSize = 32768;
    uint16_t pageSize  = 128;
    uint32_t bootWait  = config.bootloaderWaitMs;

    if (manualOverride.active) {
        flashSize = manualOverride.flashSize;
        pageSize  = manualOverride.pageSize;
        sseLog("warn", "Using manual override parameters");
    } else if (activeProfileIndex >= 0) {
        flashSize = profiles[activeProfileIndex].flashSize;
        pageSize  = profiles[activeProfileIndex].pageSize;
        bootWait  = profiles[activeProfileIndex].bootloaderWaitMs;
    }

    // ── Stage 1: Scan + validate HEX ────────────────────
    // Build a compact page-presence bitmap. 1 bit per page.
    // For 256KB / 256B = 1024 pages = 128 bytes on heap.
    programmerState = STATE_LOADING_HEX;
    sseState("loading");
    sseLog("info", "Scanning Intel HEX...");

    // Use max possible pages for the bitmap so it's valid after profile
    // is updated in Stage 3 (flashSize / pageSize may grow for Mega).
    const uint16_t bitmapPages = 2048;  // covers up to 512KB @ 256B/page
    uint8_t* pageHasData = (uint8_t*)calloc((bitmapPages + 7) / 8, 1);
    if (!pageHasData) {
        lastError = "Out of memory for page bitmap";
        sseLog("err", lastError.c_str());
        programmerState = STATE_ERROR; sseState("error");
        flashTaskHandle = NULL; vTaskDelete(NULL); return;
    }

    parser.reset();
    int lineCount = 0, scanPos = 0;
    while (scanPos < (int)currentHexData.length()) {
        if (cancelFlashRequested) {
            sseLog("warn", "Flash cancelled by user");
            lastError = "Cancelled by user";
            free(pageHasData);
            programmerState = STATE_IDLE; sseState("idle");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        int ep = currentHexData.indexOf('\n', scanPos);
        if (ep < 0) ep = currentHexData.length();
        String line = currentHexData.substring(scanPos, ep);
        line.trim();
        if (line.length() > 0) {
            HexRecord rec = parser.parseLine(line);
            if (!rec.valid) {
                snprintf(buf, sizeof(buf), "Invalid HEX at line %d", lineCount);
                sseLog("err", buf); lastError = buf;
                free(pageHasData);
                programmerState = STATE_ERROR; sseState("error");
                flashTaskHandle = NULL; vTaskDelete(NULL); return;
            }
            if (rec.type == 0x00) {
                uint32_t addr = parser.getAbsoluteAddress(rec);
                for (int i = 0; i < rec.length; i++) {
                    uint16_t pg = (uint16_t)((addr + i) / pageSize);
                    if (pg < bitmapPages) pageHasData[pg / 8] |= (1 << (pg % 8));
                }
            } else if (rec.type == 0x01) break;
            lineCount++;
        }
        scanPos = ep + 1;
    }

    // Count pages in current flashSize window for initial progress estimate
    totalPages = 0; currentPage = 0;
    for (uint16_t pg = 0; pg < (uint16_t)(flashSize / pageSize); pg++)
        if (pageHasData[pg / 8] & (1 << (pg % 8))) totalPages++;

    snprintf(buf, sizeof(buf),
        "Scanned %d lines — %d pages to write (%uB/page) — %luB flash",
        lineCount, totalPages, pageSize, (unsigned long)flashSize);
    sseLog("ok", buf);
    if (cancelFlashRequested) {
        sseLog("warn", "Flash cancelled by user");
        lastError = "Cancelled by user";
        free(pageHasData);
        programmerState = STATE_IDLE; sseState("idle");
        flashTaskHandle = NULL; vTaskDelete(NULL); return;
    }

    // ── Stage 2: Auto-detect protocol + enter prog mode ─
    programmerState = STATE_ENTERING_PROGMODE;
    sseState("entering_progmode");
    sseProgress(0, totalPages, "Detecting protocol...");

    if (manualOverride.active) {
        useV2 = (manualOverride.protocol == PROTO_STK500V2);
        snprintf(buf, sizeof(buf), "Manual override: STK500v%s", useV2 ? "2" : "1");
        sseLog("info", buf);
        bool entered = useV2 ? programmerV2->enterProgramMode(bootWait)
                             : programmer->enterProgramMode(bootWait);
        if (!entered) {
            lastError = "Failed to enter program mode";
            sseLog("err", lastError.c_str());
            free(pageHasData);
            programmerState = STATE_ERROR; sseState("error");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        enteredProgramMode = true;
    } else if (preferredProtocol == PROTO_STK500V1) {
        sseLog("info", "Protocol forced: STK500v1 (Uno/Nano)");
        if (!programmer->enterProgramMode(bootWait)) {
            lastError = "STK500v1: No device responded — check wiring";
            sseLog("err", lastError.c_str());
            free(pageHasData);
            programmerState = STATE_ERROR; sseState("error");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        enteredProgramMode = true;
        useV2 = false;
        detectedProtocol = PROTO_STK500V1;
        sseLog("ok", "STK500v1 ready");
    } else if (preferredProtocol == PROTO_STK500V2) {
        sseLog("info", "Protocol forced: STK500v2 (Mega)");
        if (!programmerV2->enterProgramMode(bootWait)) {
            lastError = "STK500v2: No device responded — check wiring";
            sseLog("err", lastError.c_str());
            free(pageHasData);
            programmerState = STATE_ERROR; sseState("error");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        enteredProgramMode = true;
        useV2 = true;
        detectedProtocol = PROTO_STK500V2;
        sseLog("ok", "STK500v2 ready");
    } else {
        sseLog("info", "Probing STK500v2 (Mega)...");
        if (programmerV2->enterProgramMode(bootWait)) {
            useV2 = true;
            enteredProgramMode = true;
            detectedProtocol = PROTO_STK500V2;
            sseLog("ok", "Protocol: STK500v2 (Mega detected)");
        } else {
            sseLog("info", "v2 not found — trying STK500v1 (Uno/Nano)...");
            if (programmer->enterProgramMode(bootWait)) {
                useV2 = false;
                enteredProgramMode = true;
                detectedProtocol = PROTO_STK500V1;
                sseLog("ok", "Protocol: STK500v1 (Uno/Nano detected)");
            } else {
                lastError = "No device responded — check wiring";
                sseLog("err", lastError.c_str());
                free(pageHasData);
                programmerState = STATE_ERROR; sseState("error");
                flashTaskHandle = NULL; vTaskDelete(NULL); return;
            }
        }
    }

    // ── Stage 3: Read + identify signature ──────────────
    programmerState = STATE_READING_SIGNATURE;
    sseState("reading_signature");
    sseProgress(0, totalPages, "Reading device signature");

    uint8_t sig[3];
    bool sigOk = useV2 ? programmerV2->getSignature(sig)
                       : programmer->getSignature(sig);
    if (!sigOk) {
        lastError = "Failed to read device signature";
        sseLog("err", lastError.c_str());
        if (useV2) programmerV2->exitProgramMode();
        else        programmer->exitProgramMode();
        free(pageHasData);
        programmerState = STATE_ERROR; sseState("error");
        flashTaskHandle = NULL; vTaskDelete(NULL); return;
    }

    detectedSig[0] = sig[0]; detectedSig[1] = sig[1]; detectedSig[2] = sig[2];
    int pidx = findProfile(sig);

    if (manualOverride.active) {
        snprintf(buf, sizeof(buf),
            "Manual override — sig 0x%02X 0x%02X 0x%02X flash=%luB page=%uB v%s",
            sig[0], sig[1], sig[2],
            (unsigned long)flashSize, pageSize, useV2 ? "2" : "1");
        sseLog("info", buf);
    } else {
        if (pidx >= 0) {
            activeProfileIndex = pidx;
            flashSize = profiles[pidx].flashSize;
            pageSize  = profiles[pidx].pageSize;
            if (profiles[pidx].protocol != PROTO_UNKNOWN)
                useV2 = (profiles[pidx].protocol == PROTO_STK500V2);
            snprintf(buf, sizeof(buf), "Identified: %s (%s) via STK500v%s",
                profiles[pidx].name, profiles[pidx].mcu, useV2 ? "2" : "1");
            sseLog("ok", buf);
            // Recount pages with updated profile flashSize/pageSize
            totalPages = 0;
            for (uint16_t pg = 0; pg < (uint16_t)(flashSize / pageSize); pg++)
                if (pageHasData[pg / 8] & (1 << (pg % 8))) totalPages++;
        } else {
            snprintf(buf, sizeof(buf),
                "Unknown sig 0x%02X 0x%02X 0x%02X — not in profile table",
                sig[0], sig[1], sig[2]);
            sseLog("warn", buf);
            sseDevice(-1, sig);
            if (useV2) programmerV2->exitProgramMode();
            else        programmer->exitProgramMode();
            free(pageHasData);
            programmerState = STATE_AWAITING_OVERRIDE;
            sseState("awaiting_override");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        sseDevice(activeProfileIndex, sig);
    }

    // ── Stage 4: Stream-flash one page at a time ─────────
    // pageBuf is the only large allocation — pageSize bytes (128 or 256).
    programmerState = STATE_PROGRAMMING;
    sseState("programming");

    uint8_t* pageBuf = (uint8_t*)malloc(pageSize);
    if (!pageBuf) {
        lastError = "Out of memory for page buffer";
        sseLog("err", lastError.c_str());
        if (useV2) programmerV2->exitProgramMode();
        else        programmer->exitProgramMode();
        free(pageHasData);
        programmerState = STATE_ERROR; sseState("error");
        flashTaskHandle = NULL; vTaskDelete(NULL); return;
    }

    uint16_t maxPages = (uint16_t)(flashSize / pageSize);
    currentPage = 0;

    for (uint16_t pg = 0; pg < maxPages; pg++) {
        if (cancelFlashRequested) {
            sseLog("warn", "Flash cancelled by user");
            lastError = "Cancelled by user";
            if (enteredProgramMode) {
                if (useV2) programmerV2->exitProgramMode();
                else       programmer->exitProgramMode();
            }
            free(pageBuf); free(pageHasData);
            programmerState = STATE_IDLE; sseState("idle");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        if (!(pageHasData[pg / 8] & (1 << (pg % 8)))) continue;

        uint32_t pageAddr = (uint32_t)pg * pageSize;
        currentPage++;

        snprintf(buf, sizeof(buf),
            "Page %d / %d  (0x%05lX)", currentPage, totalPages,
            (unsigned long)pageAddr);
        sseProgress(currentPage, totalPages, buf);

        // Fill pageBuf from HEX string for this page address range
        memset(pageBuf, 0xFF, pageSize);
        parser.reset();
        int fillPos = 0;
        while (fillPos < (int)currentHexData.length()) {
            int ep = currentHexData.indexOf('\n', fillPos);
            if (ep < 0) ep = currentHexData.length();
            String line = currentHexData.substring(fillPos, ep);
            line.trim();
            if (line.length() > 0) {
                HexRecord rec = parser.parseLine(line);
                if (rec.valid && rec.type == 0x00) {
                    uint32_t addr = parser.getAbsoluteAddress(rec);
                    for (int i = 0; i < rec.length; i++) {
                        uint32_t a = addr + i;
                        if (a >= pageAddr && a < pageAddr + pageSize)
                            pageBuf[a - pageAddr] = rec.data[i];
                    }
                } else if (rec.type == 0x01) break;
            }
            fillPos = ep + 1;
        }

        // Flash the page
        bool writeOk, verifyOk;
        if (useV2) {
            writeOk  = programmerV2->programPage(pageBuf, pageSize, pageAddr);
            verifyOk = !config.enableVerification ||
                        programmerV2->verifyPage(pageBuf, pageAddr, pageSize);
        } else {
            writeOk  = programmer->loadAddress((uint16_t)pageAddr) &&
                        programmer->programPage(pageBuf, pageSize);
            verifyOk = !config.enableVerification ||
                        programmer->verifyPage(pageBuf, (uint16_t)pageAddr, pageSize);
        }

        if (!writeOk) {
            snprintf(buf, sizeof(buf),
                "Write failed @ 0x%05lX", (unsigned long)pageAddr);
            sseLog("err", buf); lastError = buf;
            if (useV2) programmerV2->exitProgramMode();
            else        programmer->exitProgramMode();
            free(pageBuf); free(pageHasData);
            programmerState = STATE_ERROR; sseState("error");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        if (!verifyOk) {
            snprintf(buf, sizeof(buf),
                "Verify failed @ 0x%05lX", (unsigned long)pageAddr);
            sseLog("err", buf); lastError = buf;
            if (useV2) programmerV2->exitProgramMode();
            else        programmer->exitProgramMode();
            free(pageBuf); free(pageHasData);
            programmerState = STATE_ERROR; sseState("error");
            flashTaskHandle = NULL; vTaskDelete(NULL); return;
        }
        digitalWrite(config.ledPin, !digitalRead(config.ledPin));
    }

    free(pageBuf);
    free(pageHasData);
    currentHexData = "";

    // ── Stage 5: Exit ────────────────────────────────────
    programmerState = STATE_EXITING_PROGMODE;
    sseState("exiting");
    if (useV2) programmerV2->exitProgramMode();
    else        programmer->exitProgramMode();
    enteredProgramMode = false;

    programmerState = STATE_SUCCESS;
    sseState("success");
    sseLog("ok", "Flash complete — target programmed successfully");
    sseProgress(totalPages, totalPages, "Done");

    for (int i = 0; i < 6; i++) {
        digitalWrite(config.ledPin, HIGH); delay(80);
        digitalWrite(config.ledPin, LOW);  delay(80);
    }

    flashTaskHandle = NULL;
    vTaskDelete(NULL);
}

