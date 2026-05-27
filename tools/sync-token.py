#!/usr/bin/env python3
"""
sync-token.py — synchronizacja OAuth tokena z Claude Code do CLAUDE·MAN.

Tryb domyślny: watchdog (inotify/fsevents) — natychmiast reaguje na zmianę
~/.claude/.credentials.json. Jeśli biblioteka 'watchdog' nie jest zainstalowana,
przełącza się na polling co 10 min.

Wymagania: Python 3.6+, opcjonalnie: pip install watchdog
Zmienne środowiskowe:
  CLAUDE_MON_HOST     — adres urządzenia (domyślnie: claude-monitor.local)
  CLAUDE_MON_INTERVAL — interwał pollingu w sekundach (domyślnie: 600)
"""

import datetime
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

DEVICE_HOST = os.environ.get("CLAUDE_MON_HOST", "claude-monitor.local")
DEVICE_URL = f"http://{DEVICE_HOST}/token-save"
CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
POLL_INTERVAL = int(os.environ.get("CLAUDE_MON_INTERVAL", "600"))


def get_credentials() -> dict:
    """Odczytuje cały obiekt credentials z pliku."""
    with open(CREDS_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    for key in ("claudeAiOauth", "oauth", "credentials"):
        if key in data and isinstance(data[key], dict):
            return data[key]

    if isinstance(data, dict) and ("accessToken" in data or "access_token" in data):
        return data

    raise KeyError("Nie znaleziono credentials w ~/.claude/.credentials.json")


def try_refresh() -> bool:
    """Uruchamia Claude CLI w tle żeby wymusić odświeżenie tokena w credentials.json."""
    try:
        subprocess.run(
            ["claude", "--version"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
            check=False,
        )
        return True
    except Exception:
        return False


def get_token(creds: dict) -> str:
    """Wyciąga accessToken z obiektu credentials."""
    token = creds.get("accessToken") or creds.get("access_token")
    if not token:
        raise KeyError("Nie znaleziono accessToken w credentials")
    return token


def is_token_expired(creds: dict, buffer_sec: int = 300) -> bool:
    """Sprawdza czy token wygasł lub wygaśnie w ciągu buffer_sec (domyślnie 5 min)."""
    expires_at = creds.get("expiresAt")
    if not expires_at:
        return False  # brak info — wysyłamy na własną odpowiedzialność
    return (expires_at / 1000.0) < (time.time() + buffer_sec)


def sync_token(token: str) -> bool:
    """Wysyła token na urządzenie przez HTTP POST."""
    data = urllib.parse.urlencode({"key": token}).encode("utf-8")
    req = urllib.request.Request(DEVICE_URL, data=data, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return resp.status == 200
    except Exception as e:
        print(f"[sync-token] Błąd połączenia: {e}", file=sys.stderr)
        return False


# ─── Watchdog mode ────────────────────────────────────────────────────────────

class TokenHandler:
    def __init__(self):
        self._last_token = None

    def on_change(self, event):
        if event.is_directory:
            return
        if Path(event.src_path).name != ".credentials.json":
            return
        try:
            creds = get_credentials()
            if is_token_expired(creds):
                if try_refresh():
                    creds = get_credentials()  # odczytaj ponownie po odświeżeniu
                if is_token_expired(creds):
                    return  # nadal wygasł — pomijamy
            token = get_token(creds)
            if token == self._last_token:
                return
            if sync_token(token):
                self._last_token = token
                print(f"[sync-token] OK — token zsynchronizowany ({time.strftime('%H:%M:%S')})")
        except Exception as e:
            print(f"[sync-token] Błąd: {e}", file=sys.stderr)


def run_watchdog():
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler

    handler = TokenHandler()

    class _Handler(FileSystemEventHandler):
        def on_modified(self, event): handler.on_change(event)
        def on_created(self, event):  handler.on_change(event)
        def on_moved(self, event):
            if event.dest_path and Path(event.dest_path).name == ".credentials.json":
                handler.on_change(type('E', (), {'is_directory': False, 'src_path': event.dest_path})())

    observer = Observer()
    observer.schedule(_Handler(), str(CREDS_PATH.parent), recursive=False)
    observer.start()
    print(f"[sync-token] Watchdog aktywny: {CREDS_PATH}")
    print("[sync-token] Naciśnij Ctrl+C aby zatrzymać")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()


# ─── Polling fallback ─────────────────────────────────────────────────────────

def run_polling():
    print(f"[sync-token] Polling co {POLL_INTERVAL}s (watchdog niedostępny)")
    print("[sync-token] Naciśnij Ctrl+C aby zatrzymać")
    last_token = None
    while True:
        try:
            creds = get_credentials()
            if is_token_expired(creds):
                if try_refresh():
                    creds = get_credentials()
                if is_token_expired(creds):
                    time.sleep(POLL_INTERVAL)
                    continue  # nadal wygasł — pomijamy
            token = get_token(creds)
            if token != last_token:
                if sync_token(token):
                    last_token = token
                    print(f"[sync-token] OK — token zsynchronizowany ({time.strftime('%H:%M:%S')})")
        except FileNotFoundError:
            print(f"[sync-token] Nie znaleziono: {CREDS_PATH}")
        except Exception as e:
            print(f"[sync-token] Błąd: {e}", file=sys.stderr)
        try:
            time.sleep(POLL_INTERVAL)
        except KeyboardInterrupt:
            print("\n[sync-token] Zatrzymano")
            break


# ─── Oneshot mode (for systemd .path unit) ───────────────────────────────────

def run_oneshot():
    try:
        creds = get_credentials()
        if is_token_expired(creds):
            if try_refresh():
                creds = get_credentials()
            if is_token_expired(creds):
                sys.exit(0)  # nadal wygasł — kończymy bez wysyłania
        token = get_token(creds)
        if sync_token(token):
            print(f"[sync-token] OK — token zsynchronizowany ({time.strftime('%H:%M:%S')})")
        else:
            print("[sync-token] BŁĄD synchronizacji")
            sys.exit(1)
    except Exception as e:
        print(f"[sync-token] Błąd: {e}", file=sys.stderr)
        sys.exit(1)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    if "--oneshot" in sys.argv:
        run_oneshot()
        return

    try:
        import watchdog  # noqa: F401
        run_watchdog()
    except ImportError:
        print("[sync-token] Biblioteka 'watchdog' nie jest zainstalowana.")
        print("[sync-token] Zainstaluj:  python3 -m pip install watchdog")
        print("[sync-token] Przełączam na tryb polling (fallback)...\n")
        run_polling()


if __name__ == "__main__":
    main()
