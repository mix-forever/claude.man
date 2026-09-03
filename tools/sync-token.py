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

Po udanej wysyłce IP urządzenia trafia do ~/.cache/claude-man/host i służy jako
zapas, gdy rozwiązywanie nazwy .local zawiedzie.
"""

import datetime
import ipaddress
import json
import os
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

DEVICE_HOST = os.environ.get("CLAUDE_MON_HOST", "claude-monitor.local")
CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
POLL_INTERVAL = int(os.environ.get("CLAUDE_MON_INTERVAL", "600"))
# Ostatni działający adres IP — mDNS bywa zawodne (ESP32 gubi multicast),
# więc po udanej wysyłce zapamiętujemy IP i używamy go jako zapasu.
IP_CACHE_PATH = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "claude-man" / "host"
RESOLVE_TRIES = 3


def _is_ip(host: str) -> bool:
    try:
        ipaddress.ip_address(host)
        return True
    except ValueError:
        return False


def _read_cached_ip() -> str | None:
    try:
        ip = IP_CACHE_PATH.read_text(encoding="utf-8").strip()
        return ip if ip and _is_ip(ip) else None
    except OSError:
        return None


def _write_cached_ip(ip: str) -> None:
    try:
        IP_CACHE_PATH.parent.mkdir(parents=True, exist_ok=True)
        IP_CACHE_PATH.write_text(ip + "\n", encoding="utf-8")
    except OSError as e:
        print(f"[sync-token] Nie można zapisać cache IP: {e}", file=sys.stderr)


def resolve_device() -> list[str]:
    """Zwraca listę adresów do wypróbowania: rozwiązany host, potem cache IP."""
    if _is_ip(DEVICE_HOST):
        return [DEVICE_HOST]
    candidates: list[str] = []
    for attempt in range(1, RESOLVE_TRIES + 1):
        try:
            ip = socket.gethostbyname(DEVICE_HOST)
            candidates.append(ip)
            break
        except socket.gaierror:
            if attempt < RESOLVE_TRIES:
                time.sleep(1)
    cached = _read_cached_ip()
    if cached and cached not in candidates:
        if not candidates:
            print(f"[sync-token] mDNS nie odpowiada — próbuję ostatnie znane IP {cached}")
        candidates.append(cached)
    return candidates


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
    """Wysyła token na urządzenie przez HTTP POST (host mDNS, potem cache IP)."""
    data = urllib.parse.urlencode({"key": token}).encode("utf-8")
    targets = resolve_device()
    if not targets:
        print(f"[sync-token] Błąd połączenia: nie można rozwiązać {DEVICE_HOST} (brak cache IP)",
              file=sys.stderr)
        return False
    last_err: Exception | None = None
    for ip in targets:
        req = urllib.request.Request(f"http://{ip}/token-save", data=data, method="POST",
                                     headers={"Host": DEVICE_HOST})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                if resp.status == 200:
                    _write_cached_ip(ip)
                    return True
                last_err = RuntimeError(f"HTTP {resp.status} z {ip}")
        except Exception as e:  # noqa: BLE001
            last_err = e
    print(f"[sync-token] Błąd połączenia: {last_err}", file=sys.stderr)
    return False


# ─── Watchdog mode ────────────────────────────────────────────────────────────

RETRY_INTERVAL = 60  # s — ponów wysyłkę, gdy urządzenie było niedostępne


class TokenHandler:
    def __init__(self):
        self._last_token = None
        self._pending = None  # token, którego nie udało się wysłać

    def _push(self, token: str) -> None:
        if sync_token(token):
            self._last_token = token
            self._pending = None
            print(f"[sync-token] OK — token zsynchronizowany ({time.strftime('%H:%M:%S')})")
        else:
            self._pending = token
            print(f"[sync-token] Urządzenie niedostępne — ponowię za {RETRY_INTERVAL}s")

    def on_change(self, event):
        if event.is_directory:
            return
        if Path(event.src_path).name != ".credentials.json":
            return
        try:
            creds = get_credentials()
            if is_token_expired(creds):
                print(f"[sync-token] Token wygasł lub wygaśnie wkrótce. Uruchom Claude Code interaktywnie, aby odświeżyć.")
                return
            token = get_token(creds)
            if token == self._last_token:
                return
            self._push(token)
        except Exception as e:
            print(f"[sync-token] Błąd: {e}", file=sys.stderr)

    def retry(self) -> None:
        """Ponawia wysyłkę zaległego tokenu (jeśli nadal aktualny)."""
        if not self._pending:
            return
        try:
            creds = get_credentials()
            if is_token_expired(creds):
                self._pending = None
                return
            self._push(get_token(creds))
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
            time.sleep(RETRY_INTERVAL)
            handler.retry()
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
                print(f"[sync-token] Token wygasł lub wygaśnie wkrótce. Uruchom Claude Code interaktywnie, aby odświeżyć.")
                time.sleep(POLL_INTERVAL)
                continue
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
            print("[sync-token] Token wygasł lub wygaśnie wkrótce. Uruchom Claude Code interaktywnie, aby odświeżyć.")
            sys.exit(0)
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
