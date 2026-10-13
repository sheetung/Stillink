"""Settings and lifecycle for the windowless Windows broadcaster."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys

import yaml


def app_directory():
    return Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent


def load_config(path=None):
    path = Path(path) if path else app_directory() / "config.yml"
    settings = dict(target="255.255.255.255", port=8790, player="")
    if path.exists():
        data = yaml.safe_load(path.read_text(encoding="utf-8-sig")) or {}
        if not isinstance(data, dict) or set(data) - settings.keys():
            raise ValueError("config.yml 仅支持 target、port、player")
        settings.update(data)
    if not isinstance(settings["target"], str) or not settings["target"].strip():
        raise ValueError("config.yml 的 target 必须是有效地址")
    if type(settings["port"]) is not int or not 1 <= settings["port"] <= 65535:
        raise ValueError("config.yml 的 port 必须在 1–65535 之间")
    if not isinstance(settings["player"], str):
        raise ValueError("config.yml 的 player 必须是字符串")
    return settings


def setup_background_log():
    if not getattr(sys, "frozen", False):
        return
    folder = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "Stillink" / "music-client-win"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "client.log"
    if path.exists() and path.stat().st_size > 1024 * 1024:
        path.replace(folder / "client.previous.log")
    stream = path.open("a", encoding="utf-8", buffering=1)
    sys.stdout = sys.stderr = stream


class BackgroundInstance:
    """One broadcaster per Windows session; a second command can request stop."""
    def __init__(self):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateMutexW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR]
        self.api.CreateMutexW.restype = wintypes.HANDLE
        self.api.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        self.api.CreateEventW.restype = wintypes.HANDLE
        self.api.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
        self.api.OpenEventW.restype = wintypes.HANDLE
        self.api.SetEvent.argtypes = [wintypes.HANDLE]
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.mutex = self.event = None

    def acquire(self):
        self.mutex = self.api.CreateMutexW(None, False, "Local\\StillinkMusicClient")
        if not self.mutex:
            raise ctypes.WinError(ctypes.get_last_error())
        if ctypes.get_last_error() == 183:
            return False
        self.event = self.api.CreateEventW(None, True, False, "Local\\StillinkMusicClientStop")
        if not self.event:
            raise ctypes.WinError(ctypes.get_last_error())
        return True

    def stopping(self):
        return self.api.WaitForSingleObject(self.event, 0) == 0

    def stop(self):
        event = self.api.OpenEventW(2, False, "Local\\StillinkMusicClientStop")
        if event:
            try: self.api.SetEvent(event)
            finally: self.api.CloseHandle(event)

    def close(self):
        for handle in (self.event, self.mutex):
            if handle: self.api.CloseHandle(handle)
