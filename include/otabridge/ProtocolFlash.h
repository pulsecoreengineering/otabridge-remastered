#pragma once

#include "otabridge/AppState.h"

void flashTask(void* param);
void initProgrammers(uint32_t baud, int resetPin);
void restartProgrammers(uint32_t baud);

// Streaming HEX ingest — incoming hex TEXT (local /api/upload-hex chunks or
// relay program_chunk pieces) is written straight to LittleFS instead of
// being buffered in a RAM String, so a fully-packed Mega image (~700KB of
// hex text) doesn't need a matching contiguous heap allocation. flashTask()
// streams it back off disk in two sequential passes — see 03_protocol_flash.inl.
bool   hexIngestBegin();                             // (re)creates the ingest file for writing
bool   hexIngestAppend(const uint8_t* data, size_t len);
void   hexIngestEnd();                               // closes the write handle
size_t hexIngestSize();                               // 0 if no file / empty
void   hexIngestClear();                              // deletes the file
