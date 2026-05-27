#include "wifi_mgr.h"
#include <WiFi.h>
#include <string.h>

bool wifiStartAP(const char* ssid, const char* pass) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    bool ok;
    if (strlen(pass) >= 8) {
        ok = WiFi.softAP(ssid, pass);
    } else {
        ok = WiFi.softAP(ssid);  // open network
    }
    return ok;
}

bool wifiConnectSTA(const char* ssid, const char* pass, uint32_t timeoutMs) {
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(200);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;
        delay(100);
    }
    return true;
}

bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifiLocalIP() {
    return WiFi.localIP().toString();
}
