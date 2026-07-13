// ─── WiFi ────────────────────────────────────────────
// ─── Provision Web Server ─────────────────────────────
// Only runs in AP/setup mode. Serves a single-page setup UI at 192.168.4.1
// Main programmer web server does NOT start in this mode.
void setupProvisionServer() {
    // Inline HTML for the provisioning page — self-contained, no LittleFS dependency
    static const char PROVISION_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>ESP32 Programmer — Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',system-ui,sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
  .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:32px;width:100%;max-width:480px}
  .logo{display:flex;align-items:center;gap:12px;margin-bottom:28px}
  .hex{width:36px;height:36px;background:linear-gradient(135deg,#f0a500,#e08000);clip-path:polygon(50% 0%,100% 25%,100% 75%,50% 100%,0% 75%,0% 25%);flex-shrink:0}
  .brand{font-size:15px;font-weight:600;color:#e6edf3}
  .brand span{color:#f0a500}
  h1{font-size:18px;font-weight:600;margin-bottom:6px}
  .sub{font-size:12px;color:#8b949e;margin-bottom:24px}
  .section{margin-bottom:20px}
  .section-title{font-size:11px;font-weight:600;color:#f0a500;text-transform:uppercase;letter-spacing:.06em;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #21262d}
  .field{margin-bottom:12px}
  label{display:block;font-size:12px;color:#8b949e;margin-bottom:4px}
  input{width:100%;background:#0d1117;border:1px solid #30363d;border-radius:4px;padding:8px 10px;color:#e6edf3;font-size:13px;font-family:inherit;outline:none;transition:border-color .15s}
  input:focus{border-color:#f0a500}
  .secret-row{display:flex;gap:6px}
  .secret-row input{font-family:'Courier New',monospace;font-size:12px;color:#58a6ff}
  .btn-regen{background:#21262d;border:1px solid #30363d;border-radius:4px;padding:8px 12px;color:#8b949e;font-size:11px;cursor:pointer;white-space:nowrap;transition:all .15s}
  .btn-regen:hover{border-color:#f0a500;color:#f0a500}
  .btn-save{width:100%;background:#f0a500;border:none;border-radius:4px;padding:11px;color:#0d1117;font-size:14px;font-weight:600;cursor:pointer;margin-top:8px;transition:background .15s}
  .btn-save:hover{background:#d4940a}
  .btn-save:disabled{background:#3d3206;color:#6e5a00;cursor:not-allowed}
  .msg{margin-top:14px;padding:10px 12px;border-radius:4px;font-size:12px;display:none}
  .msg.ok{background:rgba(35,134,54,.15);border:1px solid rgba(35,134,54,.4);color:#3fb950;display:block}
  .msg.err{background:rgba(248,81,73,.1);border:1px solid rgba(248,81,73,.3);color:#f85149;display:block}
  .chip-id{font-size:11px;color:#484f58;text-align:center;margin-top:18px}
  .chip-id span{color:#58a6ff;font-family:monospace}
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <div class="hex"></div>
    <div class="brand">Pulse<span>Core</span> Engineering</div>
  </div>
  <h1>Device Setup</h1>
  <p class="sub">Connect this programmer to your WiFi and set a device secret. The secret is your API key for the web interface and SDK — keep a copy of it.</p>

  <div class="section">
    <div class="section-title">Device Identity</div>
    <div class="field">
      <label>Device Name</label>
      <input id="deviceName" type="text" placeholder="e.g. Programmer-A1B2" maxlength="32"/>
    </div>
    <div class="field">
      <label>Device Secret <span style="color:#484f58">(API key for all write operations)</span></label>
      <div class="secret-row">
        <input id="deviceSecret" type="text" maxlength="64" spellcheck="false"/>
        <button class="btn-regen" onclick="regen()">↻ New</button>
      </div>
    </div>
  </div>

  <div class="section">
    <div class="section-title">WiFi Network</div>
    <div class="field">
      <label>Network <span style="color:#484f58" id="scanState">(scanning…)</span></label>
      <div class="secret-row">
        <select id="ssidSelect" onchange="onPick()" style="flex:1;background:#0d1117;border:1px solid #30363d;border-radius:4px;padding:8px 10px;color:#e6edf3;font-size:13px;outline:none">
          <option value="">— select a network —</option>
          <option value="__manual__">Other / hidden network…</option>
        </select>
        <button class="btn-regen" onclick="doScan()">↻ Rescan</button>
      </div>
    </div>
    <div class="field" id="manualSsidField" style="display:none">
      <label>SSID</label>
      <input id="wifiSSID" type="text" placeholder="Network name" autocomplete="off"/>
    </div>
    <div class="field">
      <label>Password</label>
      <input id="wifiPassword" type="password" placeholder="WiFi password" autocomplete="off"/>
    </div>
  </div>

  <button class="btn-save" id="saveBtn" onclick="save()">Test &amp; Save</button>
  <div class="msg" id="msg"></div>
  <div class="chip-id">Chip ID: <span id="chipId">—</span></div>
</div>
<script>
function selectedSSID(){
  const sel=document.getElementById('ssidSelect').value;
  if(sel==='__manual__') return document.getElementById('wifiSSID').value.trim();
  return sel;
}
function onPick(){
  const sel=document.getElementById('ssidSelect').value;
  document.getElementById('manualSsidField').style.display=(sel==='__manual__')?'block':'none';
}
async function load(){
  try{
    const r=await fetch('/provision/info');
    const d=await r.json();
    document.getElementById('chipId').textContent=d.chipId||'—';
    document.getElementById('deviceName').value=d.deviceName||'';
    document.getElementById('deviceSecret').value=d.deviceSecret||'';
    // never pre-fill password for security
  }catch(e){showMsg('Could not load current config: '+e.message,'err');}
  doScan();
}
async function doScan(){
  const st=document.getElementById('scanState');
  st.textContent='(scanning…)';
  for(let attempt=0;attempt<6;attempt++){
    try{
      const r=await fetch('/provision/scan');
      const d=await r.json();
      if(d.scanning){ await new Promise(x=>setTimeout(x,2000)); continue; }
      const sel=document.getElementById('ssidSelect');
      const keep=sel.value;
      sel.innerHTML='<option value="">— select a network —</option>';
      (d.networks||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
        const o=document.createElement('option');
        o.value=n.ssid;
        const bars=n.rssi>-60?'▂▄▆█':n.rssi>-70?'▂▄▆':n.rssi>-80?'▂▄':'▂';
        o.textContent=`${n.ssid}  ${bars}${n.open?'  (open)':''}`;
        sel.appendChild(o);
      });
      const man=document.createElement('option');
      man.value='__manual__';man.textContent='Other / hidden network…';
      sel.appendChild(man);
      if(keep) sel.value=keep;
      st.textContent=`(${(d.networks||[]).length} found)`;
      return;
    }catch(e){ await new Promise(x=>setTimeout(x,1500)); }
  }
  st.textContent='(scan failed — use Other)';
}
async function regen(){
  try{
    const r=await fetch('/provision/secret');
    const d=await r.json();
    document.getElementById('deviceSecret').value=d.secret;
  }catch(e){}
}
async function save(force){
  const btn=document.getElementById('saveBtn');
  const name=document.getElementById('deviceName').value.trim();
  const secret=document.getElementById('deviceSecret').value.trim();
  const ssid=selectedSSID();
  const pass=document.getElementById('wifiPassword').value;
  if(!ssid){showMsg('Pick a network (or choose Other and type the SSID).','err');return;}
  if(!secret||secret.length<8){showMsg('Secret must be at least 8 characters','err');return;}
  btn.disabled=true;btn.textContent=force?'Saving…':'Testing connection…';
  try{
    const r=await fetch('/provision/save',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({deviceName:name,deviceSecret:secret,wifiSSID:ssid,wifiPassword:pass,force:!!force})
    });
    const d=await r.json();
    if(d.success){
      let where = d.ip
        ? `Reach it at <b>http://${d.mdns}</b> or <b>http://${d.ip}</b>`
        : 'It will join your network on restart.';
      showMsg(`✓ ${d.message}<br>${where}<br><span style="color:#8b949e">Enter your device secret as the API key on the main page.</span>`,'ok');
      btn.textContent='Done — restarting';
    } else if(d.connected===false){
      // Test failed — offer a force-save
      showMsg(`${d.message} <a href="#" style="color:#f0a500" onclick="save(true);return false;">Save anyway →</a>`,'err');
      btn.disabled=false;btn.textContent='Test & Save';
    } else {
      showMsg('Save failed: '+(d.message||'unknown error'),'err');
      btn.disabled=false;btn.textContent='Test & Save';
    }
  }catch(e){
    showMsg('Error: '+e.message,'err');
    btn.disabled=false;btn.textContent='Test & Save';
  }
}
function showMsg(text,type){
  const el=document.getElementById('msg');
  el.innerHTML=text;el.className='msg '+type;
}
load();
</script>
</body>
</html>
)rawhtml";

    // GET / → serve provision page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", PROVISION_HTML);
    });

    // GET /provision/info → current values + chip ID
    server.on("/provision/info", HTTP_GET, [](AsyncWebServerRequest* req) {
        DynamicJsonDocument doc(512);
        // Chip ID from MAC
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        char chipId[18];
        snprintf(chipId, sizeof(chipId), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        doc["chipId"]       = chipId;
        doc["deviceName"]   = provision.deviceName;
        doc["deviceSecret"] = provision.deviceSecret;
        doc["wifiSSID"]     = provision.wifiSSID;
        // never send wifiPassword back
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // GET /provision/secret → generate a fresh secret
    server.on("/provision/secret", HTTP_GET, [](AsyncWebServerRequest* req) {
        String s = generateSecret();
        req->send(200, "application/json", "{\"secret\":\"" + s + "\"}");
    });

    // GET /provision/scan → list nearby networks so the user picks instead of typing
    server.on("/provision/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            // Kick off an async scan; client polls again in ~2s
            WiFi.scanNetworks(true /*async*/);
            req->send(202, "application/json", "{\"scanning\":true}");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            req->send(202, "application/json", "{\"scanning\":true}");
            return;
        }
        DynamicJsonDocument doc(2048);
        doc["scanning"] = false;
        JsonArray arr = doc.createNestedArray("networks");
        // De-dup by SSID, keep strongest RSSI
        for (int i = 0; i < n && arr.size() < 20; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            bool seen = false;
            for (JsonObject o : arr) if (o["ssid"] == ssid) { seen = true; break; }
            if (seen) continue;
            JsonObject o = arr.createNestedObject();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
            o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /provision/save → TEST credentials, then commit.
    // Body: {deviceName, deviceSecret, wifiSSID, wifiPassword}
    // Response reports whether the STA connection succeeded and, if so, the
    // acquired IP + mDNS name — so the user knows where the device will live.
    // Only on success (or explicit force) do we persist and restart.
    server.on("/provision/save", HTTP_POST,
        [](AsyncWebServerRequest* req){},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json",
                    "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }
            String name   = doc["deviceName"]   | provision.deviceName;
            String secret = doc["deviceSecret"] | "";
            String ssid   = doc["wifiSSID"]     | "";
            String pass   = doc["wifiPassword"] | "";
            bool   force  = doc["force"]        | false;   // commit even if test fails

            if (secret.length() < 8) {
                req->send(400, "application/json",
                    "{\"success\":false,\"message\":\"Secret must be at least 8 characters\"}");
                return;
            }
            if (ssid.length() == 0) {
                req->send(400, "application/json",
                    "{\"success\":false,\"message\":\"WiFi SSID required\"}");
                return;
            }

            // ── Test the connection (AP stays up; we are in AP_STA) ──
            String acquiredIP, mdnsHost;
            bool connected = false;
            if (!force) {
                // Cancel any in-flight async scan — a scan and a connect
                // must not run on the STA interface simultaneously.
                if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
                    WiFi.scanDelete();
                    delay(100);
                }
                Serial.printf("[PROV] Testing WiFi: %s ...\n", ssid.c_str());
                WiFi.begin(ssid.c_str(), pass.c_str());
                unsigned long start = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
                    delay(200);
                }
                connected = (WiFi.status() == WL_CONNECTED);
                if (connected) {
                    acquiredIP = WiFi.localIP().toString();
                    mdnsHost   = "otabridge-" + deviceIdSuffix();
                    mdnsHost.toLowerCase();
                    Serial.printf("[PROV] Test OK — IP %s\n", acquiredIP.c_str());
                } else {
                    Serial.println("[PROV] Test FAILED — not committing");
                    WiFi.disconnect(false, true);
                }
            }

            if (!connected && !force) {
                DynamicJsonDocument r(256);
                r["success"]   = false;
                r["connected"] = false;
                r["message"]   = "Could not connect to that network — check the "
                                 "password and try again. (Nothing was saved.)";
                String out; serializeJson(r, out);
                req->send(200, "application/json", out);
                return;
            }

            // ── Commit ──
            provision.deviceName   = name.length() ? name : defaultDeviceName();
            provision.deviceSecret = secret;
            provision.wifiSSID     = ssid;
            provision.wifiPassword = pass;
            if (!saveProvision()) {
                req->send(500, "application/json",
                    "{\"success\":false,\"message\":\"LittleFS write failed\"}");
                return;
            }

            DynamicJsonDocument r(384);
            r["success"]    = true;
            r["connected"]  = connected;
            r["deviceName"] = provision.deviceName;
            if (connected) {
                r["ip"]    = acquiredIP;
                r["mdns"]  = mdnsHost + ".local";
            }
            r["message"] = connected
                ? "Connected and saved. The device is restarting on your network."
                : "Saved (unverified). The device is restarting.";
            String out; serializeJson(r, out);
            req->send(200, "application/json", out);

            delay(600);
            ESP.restart();
        }
    );

    server.begin();
    Serial.println("Provision server running at http://192.168.4.1");
}

void setupWiFi() {
    // Only called in normal (provisioned) mode — setup() guarantees
    // provision.wifiSSID and provision.deviceSecret exist.
    WiFi.mode(WIFI_STA);
    WiFi.begin(provision.wifiSSID.c_str(), provision.wifiPassword.c_str());
    Serial.printf("Connecting to WiFi: %s", provision.wifiSSID.c_str());
    int n = 0;
    while (WiFi.status() != WL_CONNECTED && n++ < 20) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nSTA  IP: %s\n", WiFi.localIP().toString().c_str());
        // Per-device mDNS name — multiple bridges on one LAN must not collide
        String host = "otabridge-" + deviceIdSuffix();
        host.toLowerCase();
        if (MDNS.begin(host.c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local\n", host.c_str());
        }
    } else {
        // Failed to connect — fall back to the setup AP so the user can
        // fix credentials. isProvisionMode makes setup() start the
        // provision server instead of the main web server.
        Serial.println("\nWiFi failed — falling back to setup AP");
        String apSSID = "OTABridge-Setup-" + deviceIdSuffix();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSSID.c_str(), "setup1234");
        Serial.printf("Setup AP: %s  IP: %s\n",
            apSSID.c_str(), WiFi.softAPIP().toString().c_str());
        isProvisionMode = true;
    }
}

// ─── Serial CLI ──────────────────────────────────────
// Provision from a raw serial line: "setprov <ssid> <pass> [name] [secret]".
// Quotes optional; use "" for an open-network password.
static void handleSetProv(const String& raw) {
    // Tokenise, honoring double-quoted fields.
    String tok[4]; int nt = 0; int i = 0; int len = raw.length();
    while (i < len && nt < 4) {
        while (i < len && raw[i] == ' ') i++;
        if (i >= len) break;
        String cur = "";
        if (raw[i] == '"') {
            i++;
            while (i < len && raw[i] != '"') cur += raw[i++];
            if (i < len) i++; // closing quote
        } else {
            while (i < len && raw[i] != ' ') cur += raw[i++];
        }
        tok[nt++] = cur;
    }
    if (nt < 2) {
        Serial.println("Usage: setprov <ssid> <pass> [name] [secret]");
        Serial.println("  open network: setprov MySSID \"\"");
        return;
    }
    provision.wifiSSID     = tok[0];
    provision.wifiPassword = tok[1];
    provision.deviceName   = (nt >= 3 && tok[2].length()) ? tok[2] : defaultDeviceName();
    provision.deviceSecret = (nt >= 4 && tok[3].length() >= 8) ? tok[3] : generateSecret();

    if (!saveProvision()) { Serial.println("setprov: LittleFS write FAILED"); return; }
    Serial.println("Provisioned:");
    Serial.printf("  Device name: %s\n", provision.deviceName.c_str());
    Serial.printf("  WiFi SSID:   %s\n", provision.wifiSSID.c_str());
    Serial.printf("  Secret:      %s\n", provision.deviceSecret.c_str());
    Serial.println("  (save the secret — it is your API key). Restarting...");
    delay(400);
    ESP.restart();
}

void processSerialCommands() {
    if (!Serial.available()) return;
    String raw = Serial.readStringUntil('\n');
    raw.trim();
    // setprov keeps original case (SSIDs/passwords/secrets are case-sensitive)
    if (raw.startsWith("setprov")) {
        String rest = raw.substring(String("setprov").length());
        rest.trim();
        handleSetProv(rest);
        return;
    }
    String cmd = raw;
    cmd.trim(); cmd.toLowerCase();
    if (cmd == "status") {
        Serial.printf("State:%d Page:%d/%d Sig:0x%02X 0x%02X 0x%02X\n",
            programmerState, currentPage, totalPages,
            detectedSig[0], detectedSig[1], detectedSig[2]);
    } else if (cmd == "profiles") {
        Serial.printf("%d profiles:\n", profileCount);
        for (int i = 0; i < profileCount; i++)
            Serial.printf("  [%d] %s (%s) sig=0x%02X 0x%02X 0x%02X %s\n",
                i, profiles[i].name, profiles[i].mcu,
                profiles[i].sig[0], profiles[i].sig[1], profiles[i].sig[2],
                profiles[i].isCustom ? "[custom]" : "[built-in]");
    } else if (cmd == "wifi") {
        Serial.printf("Mode:%s IP:%s\n",
            WiFi.getMode()==WIFI_AP ? "AP" : "STA",
            WiFi.getMode()==WIFI_AP
                ? WiFi.softAPIP().toString().c_str()
                : WiFi.localIP().toString().c_str());
    } else if (cmd == "provision") {
        // Show current provision config
        Serial.printf("Provisioned: %s\n", provision.provisioned ? "yes" : "no");
        Serial.printf("Device name: %s\n", provision.deviceName.c_str());
        Serial.printf("WiFi SSID:   %s\n", provision.wifiSSID.c_str());
        Serial.printf("Secret:      %s\n", provision.deviceSecret.c_str());
        Serial.printf("Current IP:  %s\n",
            WiFi.getMode()==WIFI_AP
                ? WiFi.softAPIP().toString().c_str()
                : WiFi.localIP().toString().c_str());
    } else if (cmd == "delprovision") {
        // Delete provision config and reboot into setup mode
        if (LittleFS.exists("/provision.json")) {
            LittleFS.remove("/provision.json");
            Serial.println("provision.json deleted — rebooting into setup mode...");
        } else {
            Serial.println("No provision.json found — rebooting into setup mode anyway...");
        }
        delay(300);
        ESP.restart();
    } else if (cmd == "scan") {
        Serial.println("Scanning WiFi networks...");
        int n = WiFi.scanNetworks();
        if (n <= 0) {
            Serial.println("  (none found)");
        } else {
            for (int i = 0; i < n; i++) {
                Serial.printf("  %2d  %-32s  %4d dBm  %s\n",
                    i, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
            }
        }
        WiFi.scanDelete();
    } else if (cmd == "restart") {
        Serial.println("Restarting..."); delay(200); ESP.restart();
    } else if (cmd == "help") {
        Serial.println("Commands:");
        Serial.println("  status        — programmer state");
        Serial.println("  profiles      — list device profiles");
        Serial.println("  wifi          — WiFi mode and IP");
        Serial.println("  scan          — list nearby WiFi networks");
        Serial.println("  provision     — show provision config + current IP");
        Serial.println("  setprov <ssid> <pass> [name] [secret]");
        Serial.println("                — provision non-interactively (bench/factory)");
        Serial.println("  delprovision  — delete provision config, reboot to setup mode");
        Serial.println("  restart       — reboot");
        if (isProvisionMode)
            Serial.println("  [device is in SETUP mode — open http://192.168.4.1]");
    } else {
        Serial.println("Unknown — type 'help'");
    }
}

// ─── Setup ───────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.printf("\nOTABridge v%s\n", OTABRIDGE_FW_VERSION);
    Serial.println("=========================");

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed — formatting...");
        LittleFS.format();
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS ERROR — cannot continue"); return;
        }
    }
    // Verify root is writable by testing a small file
    {
        File t = LittleFS.open("/test_write.tmp", "w");
        if (!t) {
            Serial.println("LittleFS root not writable — reformatting...");
            LittleFS.end();
            LittleFS.format();
            LittleFS.begin(true);
        } else {
            t.close();
            LittleFS.remove("/test_write.tmp");
            Serial.println("LittleFS OK");
        }
    }

    // ── Provision check ──────────────────────────────
    // Boot button held → force setup mode regardless of saved config
    bool forceProvision = bootButtonHeld();
    if (forceProvision) {
        Serial.println("BOOT button held — forcing setup mode");
    }

    bool provisioned = loadProvision();

    if (!provisioned || forceProvision) {
        // Setup mode — start AP, serve provision page only
        isProvisionMode = true;

        // Pre-populate defaults for first boot
        if (provision.deviceName.length() == 0)
            provision.deviceName = defaultDeviceName();
        if (provision.deviceSecret.length() == 0)
            provision.deviceSecret = generateSecret();

        Serial.println("Setup mode — starting provision AP");
        String apSSID = "OTABridge-Setup-" + deviceIdSuffix();
        // AP_STA: the AP stays up for the setup UI while we can also bring the
        // STA interface up to TEST the user's WiFi credentials before committing.
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apSSID.c_str(), "setup1234");
        Serial.printf("Connect to: %s\n", apSSID.c_str());
        Serial.printf("Password:   setup1234\n");
        Serial.printf("Open:       http://192.168.4.1\n");

        setupProvisionServer();
        // Loop handled in loop() — nothing else starts
        return;
    }

    // ── Normal mode ──────────────────────────────────
    Serial.printf("Device: %s\n", provision.deviceName.c_str());

    loadConfig();
    loadBuiltinProfiles();
    loadCustomProfiles();

    pinMode(config.ledPin, OUTPUT);
    digitalWrite(config.ledPin, LOW);

    // Shared serial/programmer init lives in ProtocolFlash module.
    initProgrammers(config.baudRate, config.resetPin);

    setupWiFi();
    if (isProvisionMode) {
        // WiFi fallback path — serve the setup page, not the main UI
        setupProvisionServer();
        return;
    }
    setupWebServer();

    Serial.printf("Ready. %d device profiles loaded.\n", profileCount);
    Serial.println("Type 'help' for serial commands.");
}

// ─── Loop ────────────────────────────────────────────
void loop() {
    if (isProvisionMode) {
        // Setup mode — the async web server runs on its own, but we still
        // service the serial CLI so 'help', 'scan', 'setprovision' etc. work.
        processSerialCommands();
        delay(10);
        return;
    }
    processSerialCommands();
    processDebugSerial();
    static unsigned long lastBlink = 0;
    if (programmerState == STATE_IDLE && millis() - lastBlink > 2000) {
        digitalWrite(config.ledPin, HIGH); delay(50);
        digitalWrite(config.ledPin, LOW);
        lastBlink = millis();
    }
    delay(10);
}
