#pragma once
#include <Arduino.h>
#include <time.h>

struct RateLimit {
    float  util5h    = 0.0f;
    float  util7d    = 0.0f;
    time_t reset5hAt = 0;
    time_t reset7dAt = 0;
    bool   valid     = false;
};

// token = accessToken from ~/.claude/.credentials.json
// httpCode receives raw HTTP status (negative = connection error)
bool apiFetch(const char* token, RateLimit& out, int& httpCode);
