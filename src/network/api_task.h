#pragma once
#include "api_client.h"

void apiTaskStart(const char* token);
void apiTaskUpdateToken(const char* token);
bool apiTaskGetResult(RateLimit& out, bool& apiOK, int& lastHttp);
bool apiTaskHasResult();
