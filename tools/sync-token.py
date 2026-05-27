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

import json
import os
import sys
import time
import urllib.request
from pathlib import Path

DEVICE_HOST = os.environ.get("CLAUDE_MON_HOST", "claude-monitor.local")
DEVICE_URL = f"http://{DEVICE_HOST}/token-save"
CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
POLL_INTERVAL = int(os.environ.get("CLAUDE_MON_INTERVAL", "600"))


def get_token() -> str:
    """Odczytuje accessToken z credentials.json (wspiera różne struktury)."""
    with open(CREDS_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    for key in ("claudeAiOauth", "oauth", "credentials"):
        if key in data and isinstance(data[key], dict):
            token = data[key].get("accessToken") or data[key].get("access_token")
            if token:
                return token

    token = data.get("accessToken") or data.get("access_token")
    if token:
        return token

    raise KeyError("Nie znaleziono accessToken w ~/.claude/.credentials.json")


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
            token = get_token()
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
            token = get_token()
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
        token = get_token()
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
