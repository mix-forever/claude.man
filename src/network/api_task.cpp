#include "api_task.h"
#include "../../include/config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static RateLimit         g_rl;
static bool              g_ok     = false;
static int               g_code   = 0;
static bool              g_hasNew = false;
static SemaphoreHandle_t g_mtx    = nullptr;
static char              g_token[513];
static TaskHandle_t      g_task   = nullptr;

static void apiPollTask(void* /*param*/) {
    char localToken[513];
    while (true) {
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            strncpy(localToken, g_token, sizeof(localToken));
            localToken[sizeof(localToken) - 1] = '\0';
            xSemaphoreGive(g_mtx);
        }

        RateLimit rl;
        int code = 0;
        bool ok = false;
        if (localToken[0] == '\0') {
            code = 401;  // no token yet — don't bother the API
        } else {
            ok = apiFetch(localToken, rl, code);
        }

        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
            // Rate-limit headers are present even on 429/401 — keep them when parsed
            if (rl.valid) g_rl = rl;
            g_ok     = ok;
            g_code   = ok ? 0 : code;
            g_hasNew = true;
            xSemaphoreGive(g_mtx);
        }

        uint32_t delayMs = API_POLL_MS;
        if (!ok && code == 429) {
            delayMs = 300000;  // 5 min backoff na rate-limit
        }
        // Czekaj na delay LUB powiadomienie (np. po aktualizacji tokenu)
        ulTaskNotifyTake(pdTRUE, delayMs / portTICK_PERIOD_MS);
    }
}

void apiTaskStart(const char* token) {
    if (g_mtx == nullptr) {
        g_mtx = xSemaphoreCreateMutex();
    }
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
        strncpy(g_token, token, sizeof(g_token) - 1);
        g_token[sizeof(g_token) - 1] = '\0';
        xSemaphoreGive(g_mtx);
    }

    xTaskCreate(apiPollTask, "apiPoll", 12288, nullptr, 1, &g_task);
}

void apiTaskUpdateToken(const char* token) {
    if (g_mtx == nullptr) return;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
        strncpy(g_token, token, sizeof(g_token) - 1);
        g_token[sizeof(g_token) - 1] = '\0';
        xSemaphoreGive(g_mtx);
    }
    // Obudź task natychmiast — nowy token zostanie użyty od razu
    if (g_task != nullptr) {
        xTaskNotifyGive(g_task);
    }
}

bool apiTaskGetResult(RateLimit& out, bool& apiOK, int& lastHttp) {
    if (g_mtx == nullptr) return false;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) != pdTRUE) return false;
    out      = g_rl;
    apiOK    = g_ok;
    lastHttp = g_code;
    g_hasNew = false;
    xSemaphoreGive(g_mtx);
    return true;
}

bool apiTaskHasResult() {
    if (g_mtx == nullptr) return false;
    bool v = false;
    if (xSemaphoreTake(g_mtx, 0) == pdTRUE) {
        v = g_hasNew;
        xSemaphoreGive(g_mtx);
    }
    return v;
}

bool apiTaskHasToken() {
    if (g_mtx == nullptr) return false;
    bool v = false;
    if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE) {
        v = g_token[0] != '\0';
        xSemaphoreGive(g_mtx);
    }
    return v;
}
