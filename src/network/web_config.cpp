#include "web_config.h"
#include "api_task.h"
#include <WebServer.h>
#include <Update.h>

static WebServer server(80);
static bool      pendingRestart = false;
static uint32_t  restartAt      = 0;
static bool      _started       = false;
static bool      _apMode        = false;

// ─── Shared CSS ──────────────────────────────────────────────────────────────
#define FAVICON \
"<link rel=\"icon\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='45' fill='%23FFFF00'/><polygon points='50,50 95,15 95,85' fill='%230f172a'/></svg>\">"

#define CSS \
"*{box-sizing:border-box;margin:0;padding:0}" \
"body{font-family:sans-serif;background:#0f172a;color:#e0e0e0;" \
     "min-height:100vh;display:flex;align-items:center;justify-content:center}" \
".card{width:100%;max-width:420px;padding:32px 24px}" \
"h1{color:#FFE000;font-size:20px;margin-bottom:6px}" \
"h2{color:#888;font-size:12px;font-weight:400;margin-bottom:24px}" \
".f{margin-bottom:16px}" \
"label{display:block;font-size:11px;color:#888;margin-bottom:6px;" \
      "text-transform:uppercase;letter-spacing:.5px}" \
"input{width:100%;padding:10px 12px;background:#1e293b;border:1px solid #334155;" \
      "border-radius:6px;color:#e0e0e0;font-size:15px;outline:none}" \
"input:focus{border-color:#FFE000}" \
"small{display:block;margin-top:5px;font-size:11px;color:#555}" \
"button{width:100%;margin-top:8px;padding:13px;background:#2121DE;color:#FFE000;" \
       "border:none;border-radius:6px;font-size:15px;cursor:pointer;font-weight:700}" \
"button:hover{background:#3333ff}" \
".ok{color:#07E0;font-size:32px}" \
"p{margin-top:12px;color:#888}"

// ─── Full config form (AP mode / first setup) ────────────────────────────────
static const char HTML_FULL[] PROGMEM =
"<!DOCTYPE html><html lang=pl><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Claude Monitor</title>" FAVICON "<style>" CSS "</style></head>"
"<body><div class=card>"
"<h1>Claude Monitor</h1><h2>Konfiguracja poczatkowa</h2>"
"<form method=POST action=/save enctype='multipart/form-data'>"
"<div class=f><label>Siec WiFi (SSID)</label>"
"<input name=ssid required autocomplete=off spellcheck=false></div>"
"<div class=f><label>Haslo WiFi</label>"
"<input name=pass type=password autocomplete=off></div>"
"<div class=f><label>Claude OAuth Token</label>"
"<input name=key type=password required autocomplete=off spellcheck=false>"
"<small>accessToken z ~/.claude/.credentials.json</small></div>"
"<button>Zapisz i uruchom</button>"
"</form></div></body></html>";

// ─── Token-only form (STA mode / token refresh) ──────────────────────────────
static const char HTML_TOKEN[] PROGMEM =
"<!DOCTYPE html><html lang=pl><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Aktualizacja tokenu</title>" FAVICON "<style>" CSS "</style></head>"
"<body><div class=card>"
"<h1>Claude Monitor</h1><h2>Aktualizacja tokenu OAuth</h2>"
"<form method=POST action=/token-save enctype='multipart/form-data'>"
"<div class=f><label>Nowy Claude OAuth Token</label>"
"<input name=key type=password required autocomplete=off spellcheck=false>"
"<small>accessToken z ~/.claude/.credentials.json</small></div>"
"<button>Aktualizuj token</button>"
"</form></div></body></html>";

// ─── Dashboard (STA mode) ────────────────────────────────────────────────────
static const char HTML_DASHBOARD[] PROGMEM =
"<!DOCTYPE html><html lang=pl><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>CLAUDE·MAN Dashboard</title>" FAVICON "<style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:'Courier New',monospace;background:#0B0F19;color:#e0e0e0;min-height:100vh;padding:20px;display:flex;justify-content:center}.maze-border{border:3px solid #2121DE;box-shadow:0 0 0 3px #0B0F19,0 0 0 6px #2121DE,0 0 20px rgba(33,33,222,.3);border-radius:4px}.card{width:100%;max-width:480px;background:#0f172a;overflow:hidden}.header{background:#2121DE;padding:16px 20px;text-align:center;position:relative}.header::before,.header::after{content:'';position:absolute;top:50%;width:40px;height:6px;background:repeating-linear-gradient(90deg,#FFFF00 0,#FFFF00 6px,transparent 6px,transparent 14px);transform:translateY(-50%)}.header::before{left:10px}.header::after{right:10px}.logo{font-size:22px;font-weight:bold;letter-spacing:2px;text-shadow:0 0 10px rgba(255,255,0,.5)}.cyan{color:#00FFFF}.orange{color:#FFB852}.yellow{color:#FFFF00}.section{padding:20px;border-bottom:2px solid #2121DE}.section:last-child{border-bottom:none}.section-title{color:#FFFF00;font-size:13px;text-transform:uppercase;letter-spacing:3px;margin-bottom:14px;display:flex;align-items:center;gap:8px}.section-title::before{content:'';width:8px;height:8px;background:#FFFF00;border-radius:50%;box-shadow:0 0 6px #FFFF00}label{display:block;font-size:11px;color:#888;margin-bottom:6px;text-transform:uppercase;letter-spacing:1px}input{width:100%;padding:12px 14px;background:#1e293b;border:2px solid #2121DE;border-radius:4px;color:#e0e0e0;font-family:inherit;font-size:14px;outline:none}input:focus{border-color:#FFFF00;box-shadow:0 0 8px rgba(255,255,0,.2)}input[type=file]{padding:10px;font-size:12px}input[type=file]::file-selector-button{background:#2121DE;color:#FFFF00;border:none;padding:6px 12px;border-radius:3px;font-family:inherit;cursor:pointer}button{width:100%;margin-top:12px;padding:14px;background:#2121DE;color:#FFFF00;border:none;border-radius:4px;font-family:inherit;font-size:15px;font-weight:bold;cursor:pointer;text-transform:uppercase;letter-spacing:2px;transition:all .2s}button:hover{background:#3333ff;box-shadow:0 0 15px rgba(51,51,255,.4)}.btn-ota{background:#FF0000}.btn-ota:hover{background:#ff3333;box-shadow:0 0 15px rgba(255,51,51,.4)}.hint{font-size:11px;color:#666;margin-top:8px}.hint code{color:#FFB852;background:rgba(255,184,82,.1);padding:2px 4px;border-radius:3px}.footer{text-align:center;padding:12px;font-size:10px;color:#444;border-top:1px solid #2121DE}.status{display:flex;gap:16px;margin-bottom:16px}.status-item{display:flex;align-items:center;gap:6px;font-size:12px}.dot{width:10px;height:10px;border-radius:50%;box-shadow:0 0 6px currentColor}.dot-cyan{background:#00FFFF;color:#00FFFF}.dot-pink{background:#FFB8FF;color:#FFB8FF}</style></head><body><div class=\"card maze-border\"><div class=header><div class=logo><span class=cyan>CLAUDE</span><span class=orange>·</span><span class=yellow>MAN</span></div></div><div class=section><div class=status><div class=status-item><div class=\"dot dot-cyan\"></div><span>WiFi</span></div><div class=status-item><div class=\"dot dot-pink\"></div><span>API</span></div></div></div><div class=section><div class=section-title>Token OAuth</div><form method=POST action=/token-save enctype=multipart/form-data><label>Nowy token Claude OAuth</label><input type=password name=key required autocomplete=off spellcheck=false placeholder=\"accessToken z ~/.claude/.credentials.json\"><button type=submit>Zapisz token</button></form><div class=hint>Token: <code>cat ~/.claude/.credentials.json | grep accessToken</code><br>Auto-sync: <code>python3 tools/sync-token.py</code></div></div><div class=section><div class=section-title>Firmware Update</div><form method=POST action=/update enctype=multipart/form-data><label>Plik firmware (.bin)</label><input type=file name=firmware accept=.bin required><button type=submit class=btn-ota>Aktualizuj firmware</button></form><div class=hint>Wybierz <code>firmware.bin</code> z build PlatformIO</div></div><div class=footer>claude-monitor.local · ESP32-C3 · CLAUDE·MAN</div></div></body></html>";


// ─── Success page (full config / AP mode) ────────────────────────────────────
static const char HTML_SAVED[] PROGMEM =
"<!DOCTYPE html><html><head><meta charset=utf-8>"
"<title>Zapisano</title>" FAVICON "<style>body{font-family:sans-serif;background:#0f172a;color:#e0e0e0;"
"text-align:center;padding:60px 20px}"
".ok{color:#07E000;font-size:40px;margin-bottom:16px}"
"p{color:#888;margin-top:12px}</style></head>"
"<body><div class=ok>&#10003;</div>"
"<p style='color:#FFE000;font-size:20px'>Zapisano!</p>"
"<p>Urzadzenie uruchomi sie ponownie za chwile.</p>"
"</body></html>";

// ─── Success page (token-only update, no reboot) ─────────────────────────────
static const char HTML_TOKEN_SAVED[] PROGMEM =
"<!DOCTYPE html><html><head><meta charset=utf-8>"
"<title>Token zapisany</title>" FAVICON "<style>body{font-family:sans-serif;background:#0f172a;color:#e0e0e0;"
"text-align:center;padding:60px 20px}"
".ok{color:#07E000;font-size:40px;margin-bottom:16px}"
"p{color:#888;margin-top:12px}</style></head>"
"<body><div class=ok>&#10003;</div>"
"<p style='color:#FFE000;font-size:20px'>Token zapisany!</p>"
"<p>Nastepne odpytanie API uzyje nowego tokenu.</p>"
"</body></html>";

// ─── Handlers ─────────────────────────────────────────────────────────────────

static void handleRoot() {
    if (_apMode) {
        server.send_P(200, "text/html", HTML_FULL);
    } else {
        server.send_P(200, "text/html", HTML_DASHBOARD);
    }
}

static void handleTokenPage() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

static String trimStr(const String& s) {
    String t = s;
    t.trim();
    return t;
}

static void handleSave() {
    String ssid = trimStr(server.arg("ssid"));
    String pass = trimStr(server.arg("pass"));
    String key  = trimStr(server.arg("key"));
    if (ssid.isEmpty() || key.isEmpty()) {
        server.send(400, "text/plain", "Brak wymaganych pol");
        return;
    }
    AppConfig cfg;
    strncpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
    strncpy(cfg.pass, pass.c_str(), sizeof(cfg.pass) - 1);
    cfg.pass[sizeof(cfg.pass) - 1] = '\0';
    strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
    cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\0';
    storageSave(cfg);
    server.send_P(200, "text/html", HTML_SAVED);
    pendingRestart = true;
    restartAt      = millis() + 2000;
}

static void handleTokenSave() {
    String key = trimStr(server.arg("key"));
    if (key.isEmpty()) {
        server.send(400, "text/plain", "Brak tokenu");
        return;
    }
    AppConfig cfg;
    if (!storageLoad(cfg)) {
        server.send(500, "text/plain", "Brak konfiguracji bazowej");
        return;
    }
    strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
    cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\0';
    storageSave(cfg);
    apiTaskUpdateToken(cfg.apiKey);
    server.send_P(200, "text/html", HTML_TOKEN_SAVED);
}

// ─── OTA handlers ────────────────────────────────────────────────────────────

static void handleOtaGet() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

static void handleOtaPost() {
    if (Update.hasError()) {
        server.send(500, "text/plain",
            "Update failed, code: " + String(Update.getError()));
    } else {
        server.send(200, "text/plain", "Update OK. Rebooting...");
        delay(500);
        ESP.restart();
    }
}

static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        Update.end(true);
    }
}

// ─── Public ──────────────────────────────────────────────────────────────────

void webConfigBegin(bool apMode) {
    if (_started) return;
    _started = true;
    _apMode  = apMode;
    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/token",      HTTP_GET,  handleTokenPage);
    server.on("/save",       HTTP_POST, handleSave);
    server.on("/token-save", HTTP_POST, handleTokenSave);
    server.on("/update",     HTTP_GET,  handleOtaGet);
    server.on("/update",     HTTP_POST, handleOtaPost, handleOtaUpload);
    server.begin();
}

void webConfigHandle() {
    server.handleClient();
    if (pendingRestart && millis() >= restartAt) {
        server.client().flush();
        delay(300);
        ESP.restart();
    }
}
