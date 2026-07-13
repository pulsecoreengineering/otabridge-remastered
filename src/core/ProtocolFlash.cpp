#include "otabridge/AppState.h"
#include "otabridge/RuntimeIO.h"
#include "otabridge/WebApi.h"
#include "otabridge/ProtocolFlash.h"

#include "../modules/03_protocol_flash.inl"

void initProgrammers(uint32_t baud, int resetPin) {
    Serial2.begin(baud, SERIAL_8N1, 16, 17);
    if (programmer)   { delete programmer; programmer = nullptr; }
    if (programmerV2) { delete programmerV2; programmerV2 = nullptr; }
    programmer   = new STK500Programmer(&Serial2, resetPin);
    programmerV2 = new STK500v2Programmer(&Serial2, resetPin);
}

void restartProgrammers(uint32_t baud) {
    if (programmer) {
        programmer->restart(baud);
    } else {
        Serial2.begin(baud, SERIAL_8N1, 16, 17);
    }
}
