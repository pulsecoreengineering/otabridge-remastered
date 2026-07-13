#pragma once

#include "otabridge/AppState.h"

void flashTask(void* param);
void initProgrammers(uint32_t baud, int resetPin);
void restartProgrammers(uint32_t baud);
