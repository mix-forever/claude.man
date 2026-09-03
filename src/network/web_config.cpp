#include "web_config.h"
#include "api_task.h"
#include "../../include/config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

static WebServer server(80);
static bool      pendingRestart = false;
static uint32_t  restartAt      = 0;
static bool      _started       = false;
static bool      _apMode        = false;
static String    _apSsid;
static String    _apNotice;
static bool      _apHasToken    = false;

static bool      _otaFailed     = false;
static size_t    _otaTotal      = 0;
static size_t    _otaDone       = 0;
static int       _otaPct        = -1;
static OtaProgressFn _otaCb     = nullptr;
static StatusFn      _statusFn  = nullptr;

static void otaReport(int pct) {
    if (pct == _otaPct) return;
    _otaPct = pct;
    if (_otaCb) _otaCb(pct);
}

// ─── Shared head / CSS (Pac-Man arcade theme, dark only) ─────────────────────
// Contrast on #0f172a: text #e5e7eb 14:1, muted #9aa3b2 7:1, dim #8891a3 5.7:1,
// yellow 16:1; on the blue button yellow 8.4:1; on the danger button white 7.3:1.

#define HEAD_START \
"<!DOCTYPE html><html lang=pl><head><meta charset=utf-8>" \
"<meta name=viewport content=\"width=device-width,initial-scale=1\">" \
"<meta name=theme-color content=\"#2121DE\">" \
"<link rel=icon href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='45' fill='%23FFFF00'/><polygon points='50,50 95,15 95,85' fill='%230f172a'/></svg>\">"

#define CSS R"CSS(<style>*{box-sizing:border-box;margin:0;padding:0}:root{--bg:#0B0F19;--surface:#0f172a;--field:#1e293b;--blue:#2121DE;--blue-hi:#3333ff;--yellow:#FFFF00;--orange:#FFB852;--cyan:#00FFFF;--pink:#FFB8FF;--red:#FF0000;--danger:#B00020;--ok:#22c55e;--text:#e5e7eb;--muted:#9aa3b2;--dim:#8891a3}html{color-scheme:dark}body{font-family:'JetBrains Mono','Fira Mono','DejaVu Sans Mono','Courier New',monospace;font-size:16px;line-height:1.5;background:var(--bg);color:var(--text);min-height:100dvh;padding:16px;display:flex;justify-content:center;align-items:flex-start}.card{width:100%;max-width:520px;background:var(--surface);border:3px solid var(--blue);box-shadow:0 0 0 3px var(--bg),0 0 0 6px var(--blue),0 0 20px rgba(33,33,222,.3);border-radius:4px;overflow:hidden}.header{background:var(--blue);padding:16px 20px;text-align:center;position:relative}.header::before,.header::after{content:'';position:absolute;top:50%;width:40px;height:6px;background:repeating-linear-gradient(90deg,var(--yellow) 0,var(--yellow) 6px,transparent 6px,transparent 14px);transform:translateY(-50%)}.header::before{left:12px}.header::after{right:12px}.logo{font-size:22px;font-weight:700;letter-spacing:2px;text-shadow:0 0 10px rgba(255,255,0,.5)}.cyan{color:var(--cyan)}.orange{color:var(--orange)}.yellow{color:var(--yellow)}.section{padding:20px;border-bottom:2px solid var(--blue)}.section-title{color:var(--yellow);font-size:13px;font-weight:700;text-transform:uppercase;letter-spacing:3px;margin-bottom:14px;display:flex;align-items:center;gap:8px}.section-title::before{content:'';width:8px;height:8px;background:var(--yellow);border-radius:50%;box-shadow:0 0 6px var(--yellow);flex:none}label{display:block;font-size:12px;color:var(--muted);margin-bottom:6px;text-transform:uppercase;letter-spacing:1px}.f{margin-bottom:16px}input{width:100%;min-height:44px;padding:10px 14px;background:var(--field);border:2px solid var(--blue);border-radius:4px;color:var(--text);font-family:inherit;font-size:16px}input:focus{border-color:var(--yellow);outline:none;box-shadow:0 0 8px rgba(255,255,0,.25)}input[type=file]{padding:8px;font-size:14px}input[type=file]::file-selector-button{background:var(--blue);color:var(--yellow);border:none;padding:8px 12px;border-radius:3px;font-family:inherit;cursor:pointer;margin-right:10px}.pw-wrap{position:relative}.pw-wrap input{padding-right:52px}.eye{position:absolute;right:4px;top:4px;bottom:4px;width:44px;min-height:0;margin:0;padding:0;background:transparent;border:none;color:var(--muted);cursor:pointer;border-radius:3px;display:flex;align-items:center;justify-content:center}.eye:hover,.eye[aria-pressed=true]{color:var(--yellow);background:transparent;box-shadow:none}button{width:100%;margin-top:12px;min-height:48px;padding:12px;background:var(--blue);color:var(--yellow);border:none;border-radius:4px;font-family:inherit;font-size:15px;font-weight:700;cursor:pointer;text-transform:uppercase;letter-spacing:2px;transition:background .2s,box-shadow .2s}button:hover{background:var(--blue-hi);box-shadow:0 0 15px rgba(51,51,255,.4)}button:disabled{opacity:.5;cursor:default;box-shadow:none}.btn-ota{background:var(--danger);color:#fff}.btn-ota:hover{background:#d0002a;box-shadow:0 0 15px rgba(255,0,0,.35)}:focus-visible{outline:3px solid var(--yellow);outline-offset:2px}.hint{font-size:13px;color:var(--muted);margin-top:8px;overflow-wrap:anywhere}code{color:var(--orange);background:rgba(255,184,82,.12);padding:2px 5px;border-radius:3px;font-size:13px}.msg{margin-top:12px;padding:10px 12px;border-radius:4px;font-size:14px;background:var(--field);border-left:4px solid var(--cyan)}.msg.ok{border-color:var(--ok)}.msg.err{border-color:var(--red)}.status{display:flex;gap:20px;flex-wrap:wrap;margin-bottom:16px;font-size:14px}.status-item{display:flex;align-items:center;gap:8px}.dot{width:12px;height:12px;border-radius:50%;box-shadow:0 0 6px currentColor;flex:none}.dot-cyan{background:var(--cyan);color:var(--cyan)}.dot-pink{background:var(--pink);color:var(--pink)}.dot-red{background:var(--red);color:var(--red)}.meter{margin-bottom:14px}.mrow{display:flex;align-items:baseline;gap:10px;font-size:14px;margin-bottom:6px}.mrow b{font-size:20px;color:var(--yellow);font-variant-numeric:tabular-nums}.mrow .reset{margin-left:auto;color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums}.track{height:14px;background:var(--field);border:2px solid var(--blue);border-radius:4px;overflow:hidden}.bar{height:100%;width:0;background:var(--yellow);background-image:repeating-linear-gradient(90deg,rgba(0,0,0,.25) 0,rgba(0,0,0,.25) 2px,transparent 2px,transparent 10px);transition:width .4s}.chips{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px}.chip{font-size:13px;color:var(--muted);background:var(--field);border:1px solid var(--blue);border-radius:999px;padding:4px 12px}.chip b{color:var(--text);font-weight:400}.pw{margin-top:14px}.ptrack{height:14px;background:var(--field);border:2px solid var(--blue);border-radius:4px;overflow:hidden}.pbar{height:100%;width:0;background:repeating-linear-gradient(90deg,var(--yellow) 0,var(--yellow) 8px,var(--orange) 8px,var(--orange) 12px);transition:width .2s}.pbar.err{background:var(--red)}.ptext{margin-top:6px;font-size:13px;color:var(--yellow);letter-spacing:1px}.footer{text-align:center;padding:12px;font-size:12px;color:var(--dim);line-height:1.6}.footer span{white-space:nowrap}.center{text-align:center;padding:40px 20px}.big{font-size:44px;color:var(--ok);margin-bottom:12px}.center h2{color:var(--yellow);font-size:20px;margin-bottom:10px}.center p{color:var(--muted);font-size:14px;margin-top:8px}a{color:var(--cyan)}@media(prefers-reduced-motion:reduce){*{transition:none!important}}</style>)CSS"

#define EYE_SVG "<svg width=20 height=20 viewBox=\"0 0 24 24\" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round stroke-linejoin=round aria-hidden=true><path d=\"M1 12s4-7 11-7 11 7 11 7-4 7-11 7S1 12 1 12z\"/><circle cx=12 cy=12 r=3 /></svg>"

#define LOGO "<header class=header><h1 class=logo><span class=cyan>CLAUDE</span><span class=orange>·</span><span class=yellow>MAN</span></h1></header>"

// Show/hide toggle for password fields (shared by dashboard and setup form)
#define JS_EYE R"JS(document.querySelectorAll('.eye').forEach(function(b){b.onclick=function(){var i=document.getElementById(b.getAttribute('data-for'));var s=i.type==='password';i.type=s?'text':'password';b.setAttribute('aria-pressed',s?'true':'false');b.setAttribute('aria-label',s?'Ukryj':'Pokaż');};});)JS"

// ─── Setup form (AP mode) — template: %NOTICE% %SSID% %TOKENHINT% %TOKENREQ% ──
static const char HTML_FULL[] PROGMEM = HEAD_START "<title>CLAUDE·MAN – konfiguracja</title>" CSS
"</head><body><main class=card>" LOGO
R"HTML(<section class=section><h2 class=section-title>Konfiguracja</h2>%NOTICE%
<form method=post action=/save enctype=multipart/form-data>
<div class=f><label for=ssid>Sieć Wi-Fi (SSID)</label><input id=ssid name=ssid required autocomplete=off spellcheck=false maxlength=32 value="%SSID%"></div>
<div class=f><label for=pass>Hasło Wi-Fi</label><div class=pw-wrap><input id=pass name=pass type=password autocomplete=off maxlength=64><button type=button class=eye data-for=pass aria-label="Pokaż" aria-pressed=false>)HTML" EYE_SVG R"HTML(</button></div>
<p class=hint>Zostaw puste dla sieci otwartej.</p></div>
<div class=f><label for=key>Token OAuth Claude</label><div class=pw-wrap><input id=key name=key type=password autocomplete=off spellcheck=false maxlength=512 %TOKENREQ%><button type=button class=eye data-for=key aria-label="Pokaż" aria-pressed=false>)HTML" EYE_SVG R"HTML(</button></div>
<p class=hint>%TOKENHINT%</p></div>
<button type=submit>Zapisz i uruchom</button>
</form></section>
<footer class=footer>Po zapisie urządzenie połączy się z siecią i będzie dostępne pod <code>http://claude-monitor.local/</code></footer>
</main><script>)HTML" JS_EYE "</script></body></html>";

// ─── Dashboard (STA mode) ────────────────────────────────────────────────────
static const char HTML_DASHBOARD[] PROGMEM = HEAD_START "<title>CLAUDE·MAN</title>" CSS
"</head><body><main class=card>" LOGO
R"HTML(<section class=section aria-labelledby=h-st><h2 class=section-title id=h-st>Status</h2>
<div class=status>
<div class=status-item><span id=dw class="dot dot-cyan"></span><span>Wi-Fi <b id=rssi>…</b></span></div>
<div class=status-item><span id=da class="dot dot-pink"></span><span>API <b id=api>…</b></span></div>
</div>
<div class=meter><div class=mrow><span>Sesja 5h</span><b id=p5>—</b><span id=r5 class=reset></span></div><div class=track role=progressbar aria-label="Zużycie limitu 5h" aria-valuemin=0 aria-valuemax=100 aria-valuenow=0><div id=b5 class=bar></div></div></div>
<div class=meter><div class=mrow><span>Tydzień 7d</span><b id=p7>—</b><span id=r7 class=reset></span></div><div class=track role=progressbar aria-label="Zużycie limitu 7d" aria-valuemin=0 aria-valuemax=100 aria-valuenow=0><div id=b7 class=bar></div></div></div>
<div class=chips><span class=chip>Jasność <b id=bl>—</b></span><span class=chip>Alarm <b id=al>—</b></span><span class=chip>Buzzer <b id=mu>—</b></span></div>
<p id=err class="msg err" role=alert hidden></p>
</section>
<section class=section aria-labelledby=h-tk><h2 class=section-title id=h-tk>Token OAuth</h2>
<form id=tf method=post action=/token-save enctype=multipart/form-data>
<div class=f><label for=key>Nowy token Claude OAuth</label><div class=pw-wrap><input id=key type=password name=key required autocomplete=off spellcheck=false maxlength=512 placeholder="sk-ant-oat01-…"><button type=button class=eye data-for=key aria-label="Pokaż" aria-pressed=false>)HTML" EYE_SVG R"HTML(</button></div>
<p class=hint><code>accessToken</code> z <code>~/.claude/.credentials.json</code></p><p class=hint>Auto-sync: <code>python3 tools/sync-token.py</code></p></div>
<button type=submit>Zapisz token</button>
<p id=tmsg class=msg role=status aria-live=polite hidden></p>
</form></section>
<section class=section aria-labelledby=h-fw><h2 class=section-title id=h-fw>Aktualizacja firmware</h2>
<form id=ota method=post action=/update enctype=multipart/form-data>
<div class=f><label for=fwfile>Plik firmware (.bin)</label><input id=fwfile type=file name=firmware accept=.bin required>
<p class=hint>Wybierz <code>firmware.bin</code> z builda PlatformIO. Urządzenie zrestartuje się po wgraniu.</p></div>
<button type=submit class=btn-ota>Aktualizuj firmware</button>
<div id=pw class=pw hidden><div class=ptrack><div id=bar class=pbar></div></div><p id=pt class=ptext role=status aria-live=polite></p></div>
</form></section>
<footer class=footer>firmware <span id=fwv>…</span> · uptime <span id=up>…</span><br><span id=ip>claude-monitor.local</span> · ESP32-C3</footer>
</main>
<script>
(function(){
var $=function(i){return document.getElementById(i)},st=null,at=0;
function pad(n){return (n<10?'0':'')+n}
function left(s){s=Math.max(0,Math.round(s));var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+pad(h)+'h':h?h+'h '+pad(m)+'m':m+'m '+pad(s%60)+'s'}
function up(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+pad(m)+'m'}
function col(u){return u>=.9?'#FF0000':u>=.7?'#FFB852':'#FFFF00'}
function bar(b,p,u){var pc=Math.round(u*100);$(p).textContent=pc+'%';$(p).style.color=col(u);var e=$(b);e.style.width=Math.min(100,pc)+'%';e.style.background=col(u);e.parentNode.setAttribute('aria-valuenow',pc)}
function render(){if(!st)return;var el=(Date.now()-at)/1000;
if(st.valid){bar('b5','p5',st.util5h);bar('b7','p7',st.util7d);$('r5').textContent=st.reset5h>=0?'reset za '+left(st.reset5h-el):'';$('r7').textContent=st.reset7d>=0?'reset za '+left(st.reset7d-el):''}
$('da').className='dot '+(st.apiOK?'dot-pink':'dot-red');$('api').textContent=st.apiOK?'OK':(st.hasToken?(st.http?'błąd '+st.http:'czekam…'):'brak tokenu');
$('dw').className='dot dot-cyan';$('rssi').textContent=st.rssi+' dBm';
$('bl').textContent=st.brightness+'%';$('al').textContent=st.alarmThr?st.alarmThr+'%':'wył.';$('mu').textContent=st.mute?'wyciszony':'wł.';
$('fwv').textContent=st.fw;$('up').textContent=up(st.uptime+el);$('ip').textContent=st.ip;
var e=$('err');if(!st.hasToken){e.textContent='Brak tokenu. Wklej token poniżej lub uruchom sync-token.py na komputerze z Claude Code.';e.hidden=false}
else if(!st.apiOK&&st.http==401){e.textContent='Token wygasł (HTTP 401). Wklej nowy token poniżej.';e.hidden=false}
else if(!st.apiOK&&st.http==429){e.textContent='API ograniczyło zapytania (HTTP 429). Następna próba za 5 minut.';e.hidden=false}
else e.hidden=true}
function poll(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){st=j;at=Date.now();render()}).catch(function(){$('dw').className='dot dot-red';$('rssi').textContent='brak połączenia'})}
poll();setInterval(poll,30000);setInterval(render,1000);
)HTML" JS_EYE R"HTML(
var tf=$('tf'),tm=$('tmsg');tf.onsubmit=function(e){e.preventDefault();var btn=tf.querySelector('button[type=submit]');btn.disabled=true;tm.hidden=false;tm.className='msg';tm.textContent='Zapisywanie…';
fetch('/token-save',{method:'POST',body:new FormData(tf)}).then(function(r){return r.text().then(function(t){return{ok:r.ok,t:t}})}).then(function(x){if(x.ok){tm.className='msg ok';tm.textContent='Token zapisany. Odpytuję API…';tf.reset();setTimeout(poll,4000);setTimeout(poll,12000)}else{tm.className='msg err';tm.textContent='Błąd: '+x.t}btn.disabled=false}).catch(function(){tm.className='msg err';tm.textContent='Brak połączenia z urządzeniem.';btn.disabled=false})};
var f=$('ota'),b=$('bar'),t=$('pt'),w=$('pw');f.onsubmit=function(e){e.preventDefault();var file=f.firmware.files[0];if(!file)return;var ob=f.querySelector('button[type=submit]');ob.disabled=true;w.hidden=false;b.className='pbar';t.textContent='Wysyłanie 0%';
var x=new XMLHttpRequest();x.open('POST','/update?size='+file.size);
x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.round(ev.loaded*100/ev.total);b.style.width=p+'%';t.textContent=(p<100?'Wysyłanie ':'Zapisywanie… ')+p+'%'}};
x.onload=function(){if(x.status==200){b.style.width='100%';t.textContent='Gotowe. Urządzenie się restartuje…';setTimeout(function(){var i=setInterval(function(){fetch('/status',{cache:'no-store'}).then(function(r){if(r.ok){clearInterval(i);location.reload()}}).catch(function(){})},2000)},4000)}else{b.className='pbar err';t.textContent='Błąd: '+x.responseText;ob.disabled=false}};
x.onerror=function(){b.className='pbar err';t.textContent='Błąd połączenia podczas wysyłania.';ob.disabled=false};
var fd=new FormData();fd.append('firmware',file);x.send(fd)};
})();
</script></body></html>)HTML";

// ─── Confirmation pages ──────────────────────────────────────────────────────
static const char HTML_SAVED[] PROGMEM = HEAD_START "<title>Zapisano</title>" CSS
"</head><body><main class=card>" LOGO
R"HTML(<section class=center><div class=big aria-hidden=true>&#10003;</div><h2>Zapisano</h2>
<p>Urządzenie restartuje się i łączy z Twoją siecią Wi-Fi.</p>
<p>Wróć do swojej sieci i otwórz <a href="http://claude-monitor.local/">claude-monitor.local</a>. Adres IP pojawi się też na wyświetlaczu.</p>
<p>Jeśli hasło było błędne, po trzech nieudanych próbach urządzenie wróci do tego trybu konfiguracji.</p>
</section></main></body></html>)HTML";

static const char HTML_TOKEN_SAVED[] PROGMEM = HEAD_START "<meta http-equiv=refresh content=\"3;url=/\"><title>Token zapisany</title>" CSS
"</head><body><main class=card>" LOGO
R"HTML(<section class=center><div class=big aria-hidden=true>&#10003;</div><h2>Token zapisany</h2>
<p>API zostanie odpytane od razu. Za chwilę wrócisz do <a href="/">dashboardu</a>.</p>
</section></main></body></html>)HTML";

// ─── Helpers ─────────────────────────────────────────────────────────────────

static String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

static String trimStr(const String& s) {
    String t = s;
    t.trim();
    return t;
}

// ─── Handlers ─────────────────────────────────────────────────────────────────

static void handleRoot() {
    if (!_apMode) {
        server.send_P(200, "text/html", HTML_DASHBOARD);
        return;
    }
    String page = FPSTR(HTML_FULL);
    page.replace("%NOTICE%", _apNotice.isEmpty()
        ? String("")
        : "<p class=\"msg err\" role=alert>" + _apNotice + "</p>");
    page.replace("%SSID%", htmlEscape(_apSsid));
    page.replace("%TOKENREQ%", "");
    page.replace("%TOKENHINT%", _apHasToken
        ? "Zostaw puste, aby zachować obecny token."
        : "Opcjonalnie. Możesz dodać go później w dashboardzie lub przez <code>sync-token.py</code>.");
    server.send(200, "text/html", page);
}

static void handleTokenPage() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

static void handleStatus() {
    DeviceStatus st;
    if (_statusFn) _statusFn(st);
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"valid\":%s,\"util5h\":%.3f,\"util7d\":%.3f,\"reset5h\":%ld,\"reset7d\":%ld,"
        "\"apiOK\":%s,\"http\":%d,\"hasToken\":%s,"
        "\"brightness\":%u,\"alarmThr\":%u,\"mute\":%s,"
        "\"rssi\":%d,\"ip\":\"%s\",\"uptime\":%lu,\"heap\":%u,\"fw\":\"%s\"}",
        st.valid ? "true" : "false", st.util5h, st.util7d,
        (long)st.reset5hIn, (long)st.reset7dIn,
        st.apiOK ? "true" : "false", st.lastHttp, st.hasToken ? "true" : "false",
        st.brightness, st.alarmThr, st.mute ? "true" : "false",
        WiFi.RSSI(), WiFi.localIP().toString().c_str(),
        (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
        FW_VERSION " (" __DATE__ ")");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", buf);
}

static void handleSave() {
    String ssid = trimStr(server.arg("ssid"));
    String pass = trimStr(server.arg("pass"));
    String key  = trimStr(server.arg("key"));
    if (ssid.isEmpty()) {
        server.send(400, "text/plain", "Podaj nazwę sieci Wi-Fi (SSID).");
        return;
    }
    // Start from whatever is stored so settings and an existing token survive
    // a Wi-Fi-only fix (AP fallback after failed connections).
    AppConfig cfg;
    if (!storageLoad(cfg)) {
        cfg.apiKey[0]  = '\0';
        cfg.brightness = DEFAULT_BRIGHTNESS;
        cfg.alarmThr   = DEFAULT_ALARM_THR;
        cfg.buzzerMute = false;
    }
    strncpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
    strncpy(cfg.pass, pass.c_str(), sizeof(cfg.pass) - 1);
    cfg.pass[sizeof(cfg.pass) - 1] = '\0';
    if (!key.isEmpty()) {
        strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
        cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\0';
    }
    storageSave(cfg);
    storageSetWifiFails(0);
    server.send_P(200, "text/html", HTML_SAVED);
    pendingRestart = true;
    restartAt      = millis() + 2000;
}

static void handleTokenSave() {
    String key = trimStr(server.arg("key"));
    if (key.isEmpty()) {
        server.send(400, "text/plain", "Token jest pusty.");
        return;
    }
    if (key.length() > 512) {
        server.send(400, "text/plain", "Token jest za długi (max 512 znaków).");
        return;
    }
    AppConfig cfg;
    if (!storageLoad(cfg)) {
        server.send(500, "text/plain", "Brak konfiguracji Wi-Fi. Najpierw skonfiguruj urządzenie.");
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
    if (_otaFailed || Update.hasError()) {
        _otaFailed = false;
        server.send(500, "text/plain",
            "Zapis firmware nie powiódł się (kod " + String(Update.getError()) + ").");
    } else {
        server.send(200, "text/plain", "Update OK. Rebooting...");
        delay(500);
        ESP.restart();
    }
}

static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        // Exact size comes from the dashboard JS (?size=); fall back to the
        // request Content-Length (slightly larger: multipart overhead).
        _otaTotal = (size_t)server.arg("size").toInt();
        if (_otaTotal == 0) _otaTotal = (size_t)server.clientContentLength();
        _otaDone  = 0;
        _otaPct   = -1;
        _otaFailed = !Update.begin(UPDATE_SIZE_UNKNOWN);
        otaReport(_otaFailed ? -1 : 0);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_otaFailed) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            _otaFailed = true;
            otaReport(-1);
            return;
        }
        _otaDone += upload.currentSize;
        if (_otaTotal > 0) {
            int pct = (int)((_otaDone * 100ULL) / _otaTotal);
            if (pct > 99) pct = 99;   // 100 only after Update.end() succeeds
            otaReport(pct);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_otaFailed) return;
        if (Update.end(true)) otaReport(100);
        else { _otaFailed = true; otaReport(-1); }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _otaFailed = true;
        otaReport(-1);
    }
}

// ─── Public ──────────────────────────────────────────────────────────────────

void webConfigSetOtaProgress(OtaProgressFn fn) {
    _otaCb = fn;
}

void webConfigSetStatusProvider(StatusFn fn) {
    _statusFn = fn;
}

void webConfigBegin(bool apMode, const char* prefillSsid, const char* notice, bool hasToken) {
    if (_started) return;
    _started    = true;
    _apMode     = apMode;
    _apSsid     = prefillSsid ? prefillSsid : "";
    _apNotice   = notice ? notice : "";
    _apHasToken = hasToken;
    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/status",     HTTP_GET,  handleStatus);
    server.on("/token",      HTTP_GET,  handleTokenPage);
    server.on("/save",       HTTP_POST, handleSave);
    server.on("/token-save", HTTP_POST, handleTokenSave);
    server.on("/update",     HTTP_GET,  handleOtaGet);
    server.on("/update",     HTTP_POST, handleOtaPost, handleOtaUpload);
    server.begin();
}

void webConfigHandle() {
    server.handleClient();
    if (pendingRestart && (int32_t)(millis() - restartAt) >= 0) {
        server.client().flush();
        delay(300);
        ESP.restart();
    }
}
