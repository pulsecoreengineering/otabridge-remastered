#pragma once

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_system.h>

#define RESET_PIN 4
#define LED_PIN   2

#define STK_OK              0x10
#define STK_FAILED          0x11
#define STK_UNKNOWN         0x12
#define STK_INSYNC          0x14
#define STK_NOSYNC          0x15
#define STK_GET_SYNC        0x30
#define STK_GET_SIGN_ON     0x31
#define STK_SET_PARAMETER   0x40
#define STK_GET_PARAMETER   0x41
#define STK_SET_DEVICE      0x42
#define STK_LOAD_ADDRESS    0x55
#define STK_PROG_PAGE       0x64
#define STK_READ_PAGE       0x74
#define STK_READ_SIGN       0x75
#define STK_LEAVE_PROGMODE  0x51
#define CRC_EOP             0x20

#define MAX_PROFILES 20
#define DEBUG_RX_PIN  32
#define DEBUG_TX_PIN  33

// Single source of truth for firmware version — used by /api/info,
// the boot banner, and (later) the SDK compatibility check.
#define OTABRIDGE_FW_VERSION "1.0.0"

enum ProgrammerState {
    STATE_IDLE,
    STATE_LOADING_HEX,
    STATE_ENTERING_PROGMODE,
    STATE_READING_SIGNATURE,
    STATE_AWAITING_OVERRIDE,
    STATE_PROGRAMMING,
    STATE_EXITING_PROGMODE,
    STATE_SUCCESS,
    STATE_ERROR
};

enum FlashProtocol { PROTO_UNKNOWN, PROTO_STK500V1, PROTO_STK500V2 };

struct ProvisionConfig {
    String deviceName   = "";
    String deviceSecret = "";
    String wifiSSID     = "";
    String wifiPassword = "";
    bool   provisioned  = false;
};

struct DeviceProfile {
    char          name[32];
    char          mcu[16];
    uint8_t       sig[3];
    uint32_t      flashSize;
    uint16_t      pageSize;
    uint32_t      bootloaderWaitMs;
    FlashProtocol protocol;
    bool          isCustom;
};

struct ManualOverride {
    bool          active    = false;
    uint32_t      flashSize = 32768;
    uint16_t      pageSize  = 128;
    FlashProtocol protocol  = PROTO_STK500V1;
};

struct ProgrammerConfig {
    uint32_t baudRate           = 115200;
    int      resetPin           = 4;
    int      ledPin             = 2;
    int      resetPulseMs       = 100;
    int      bootloaderWaitMs   = 750;
    int      syncAttempts       = 20;
    int      syncTimeoutMs      = 200;
    int      responseTimeoutMs  = 1000;
    bool     enableVerification = true;
    // WiFi credentials live in ProvisionConfig — single source of truth.
};

extern const DeviceProfile BUILTIN_PROFILES[];
extern const int BUILTIN_COUNT;

extern ProvisionConfig provision;
extern bool isProvisionMode;
extern DeviceProfile profiles[MAX_PROFILES];
extern int profileCount;
extern int activeProfileIndex;
extern ManualOverride manualOverride;
extern ProgrammerConfig config;
extern AsyncWebServer server;
extern AsyncEventSource events;
extern AsyncEventSource debugEvents;
extern volatile ProgrammerState programmerState;
extern String currentHexData;
extern String lastError;
extern int totalPages;
extern int currentPage;
extern TaskHandle_t flashTaskHandle;
extern uint8_t detectedSig[3];
extern volatile bool cancelFlashRequested;
extern volatile bool debugActive;
extern uint32_t debugBaudRate;
extern String debugLineBuf;
extern unsigned long debugLastHeartbeat;
extern FlashProtocol detectedProtocol;
extern FlashProtocol preferredProtocol;

String generateSecret();
String defaultDeviceName();
String deviceIdSuffix();   // 4 hex chars from factory MAC — stable per unit
bool loadProvision();
bool saveProvision();
bool bootButtonHeld();
