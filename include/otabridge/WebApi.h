#pragma once

#include "otabridge/AppState.h"

void saveConfig();
void loadConfig();

void addCORS(AsyncWebServerResponse* r);
bool hasValidApiKey(AsyncWebServerRequest* req);
bool requireApiKey(AsyncWebServerRequest* req);

void setupWebServer();
