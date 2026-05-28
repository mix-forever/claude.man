#include "api_client.h"
#include "../../include/config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char PROBE_BODY[] =
    "{\"model\":\"" API_MODEL "\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";

static bool doRequest(const char* token, RateLimit& out, int& httpCode) {
    WiFiClientSecure ssl;
    ssl.setInsecure();  // cert chain for api.anthropic.com rotates; pinning caused E-1

    HTTPClient https;
    https.setTimeout(15000);
    bool ok = false;

    if (https.begin(ssl, "https://" API_HOST "/v1/messages")) {
        const char* hdrs[] = {
            "anthropic-ratelimit-unified-5h-utilization",
            "anthropic-ratelimit-unified-5h-reset",
            "anthropic-ratelimit-unified-7d-utilization",
            "anthropic-ratelimit-unified-7d-reset"
        };
        https.collectHeaders(hdrs, 4);

        https.addHeader("Authorization",  String("Bearer ") + token);
        https.addHeader("anthropic-version", "2023-06-01");
        https.addHeader("Content-Type",   "application/json");

        httpCode = https.sendRequest("POST", PROBE_BODY);

        // Read rate-limit headers (present even on 429 / 401)
        String h5u = https.header("anthropic-ratelimit-unified-5h-utilization");
        String h5r = https.header("anthropic-ratelimit-unified-5h-reset");
        String h7u = https.header("anthropic-ratelimit-unified-7d-utilization");
        String h7r = https.header("anthropic-ratelimit-unified-7d-reset");

        if (!h5u.isEmpty() && !h5r.isEmpty() && !h7u.isEmpty() && !h7r.isEmpty()) {
            out.util5h    = h5u.toFloat();
            out.reset5hAt = (time_t)h5r.toInt();
            out.util7d    = h7u.toFloat();
            out.reset7dAt = (time_t)h7r.toInt();
            out.valid     = true;
        }
        ok = (httpCode == 200);

        https.end();
    }

    return ok;
}

bool apiFetch(const char* token, RateLimit& out, int& httpCode) {
    out      = RateLimit{};
    httpCode = 0;

    if (doRequest(token, out, httpCode)) return true;

    // Retry once on TCP/SSL errors (negative codes), not on HTTP errors (401, 429, etc.)
    if (httpCode < 0) {
        delay(2000);
        out      = RateLimit{};
        httpCode = 0;
        return doRequest(token, out, httpCode);
    }

    return false;
}
