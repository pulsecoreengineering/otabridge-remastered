#include "otabridge/AppState.h"

ProvisionConfig provision;
bool isProvisionMode = false;

const DeviceProfile BUILTIN_PROFILES[] = {
    { "Arduino Uno / Nano",   "ATmega328P", {0x1E,0x95,0x0F}, 32768,  128, 750,  PROTO_STK500V1, false },
    { "Arduino Nano (old)",   "ATmega168",  {0x1E,0x94,0x06}, 16384,  128, 750,  PROTO_STK500V1, false },
    { "Arduino Leonardo",     "ATmega32U4", {0x1E,0x95,0x87}, 32768,  128, 8000, PROTO_STK500V1, false },
    { "Arduino Pro Micro",    "ATmega32U4", {0x1E,0x95,0x87}, 32768,  128, 8000, PROTO_STK500V1, false },
    { "Arduino Pro Mini 328", "ATmega328P", {0x1E,0x95,0x0F}, 32768,  128, 750,  PROTO_STK500V1, false },
    { "Arduino Pro Mini 168", "ATmega168",  {0x1E,0x94,0x06}, 16384,  128, 750,  PROTO_STK500V1, false },
    { "Arduino Mega 2560",         "ATmega2560", {0x1E,0x98,0x01}, 262144, 256, 750, PROTO_STK500V2, false },
    { "Arduino Mega 1280",         "ATmega1280", {0x1E,0x97,0x03}, 131072, 256, 750, PROTO_STK500V2, false },
    { "Mega 2560 Clone (CH340)",   "ATmega2560", {0x1E,0x1E,0x1E}, 262144, 256, 750, PROTO_STK500V2, false },
};
const int BUILTIN_COUNT = sizeof(BUILTIN_PROFILES) / sizeof(DeviceProfile);

DeviceProfile profiles[MAX_PROFILES];
int           profileCount       = 0;
int           activeProfileIndex = -1;

ManualOverride manualOverride;
ProgrammerConfig config;
AsyncWebServer server(80);
AsyncEventSource events("/api/progress");
AsyncEventSource debugEvents("/api/debug/stream");

volatile ProgrammerState programmerState = STATE_IDLE;
String                   currentHexData  = "";
String                   lastError       = "";
int                      totalPages      = 0;
int                      currentPage     = 0;
TaskHandle_t             flashTaskHandle = NULL;
uint8_t                  detectedSig[3]  = {0, 0, 0};
volatile bool            cancelFlashRequested = false;

volatile bool   debugActive   = false;
uint32_t        debugBaudRate = 9600;
String          debugLineBuf  = "";
unsigned long   debugLastHeartbeat = 0;

String generateSecret() {
    String s = "";
    const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) s += hex[esp_random() % 16];
    return s;
}

String deviceIdSuffix() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char sfx[5];
    snprintf(sfx, sizeof(sfx), "%02X%02X", mac[4], mac[5]);
    return String(sfx);
}

String defaultDeviceName() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char name[24];
    snprintf(name, sizeof(name), "Programmer-%02X%02X", mac[4], mac[5]);
    return String(name);
}

bool loadProvision() {
    if (!LittleFS.exists("/provision.json")) return false;
    File f = LittleFS.open("/provision.json", "r");
    if (!f) return false;
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();
    provision.deviceName   = doc["deviceName"]   | "";
    provision.deviceSecret = doc["deviceSecret"] | "";
    provision.wifiSSID     = doc["wifiSSID"]     | "";
    provision.wifiPassword = doc["wifiPassword"] | "";
    provision.provisioned  = provision.wifiSSID.length() > 0 &&
                             provision.deviceSecret.length() > 0;
    return provision.provisioned;
}

bool saveProvision() {
    File f = LittleFS.open("/provision.json", "w");
    if (!f) return false;
    DynamicJsonDocument doc(512);
    doc["deviceName"]   = provision.deviceName;
    doc["deviceSecret"] = provision.deviceSecret;
    doc["wifiSSID"]     = provision.wifiSSID;
    doc["wifiPassword"] = provision.wifiPassword;
    serializeJson(doc, f);
    f.close();
    return true;
}

bool bootButtonHeld() {
    pinMode(0, INPUT_PULLUP);
    delay(10);
    int low = 0;
    for (int i = 0; i < 10; i++) {
        if (digitalRead(0) == LOW) low++;
        delay(10);
    }
    return low >= 8;
}
