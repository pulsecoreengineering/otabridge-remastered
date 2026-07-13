#pragma once

#include "otabridge/AppState.h"

void sseLog(const char* type, const char* msg);
void sseProgress(int page, int total, const char* label);
void sseState(const char* state);
void sseDevice(int pidx, uint8_t sig[3]);
void processDebugSerial();

void loadBuiltinProfiles();
void loadCustomProfiles();
void saveCustomProfiles();
int findProfile(uint8_t sig[3]);
