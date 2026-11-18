"""User settings for the playlist folder and the VLC command."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path

COLUMN_COUNT = 9
DEFAULT_COLUMN_WIDTHS = (56, 220, 150, 150, 72, 120, 72, 180, 260)


@dataclass
class Settings:
    playlist_dir: str = ""
    vlc_command: str = "vlc"
    column_widths: list[int] = field(default_factory=lambda: list(DEFAULT_COLUMN_WIDTHS))


def config_file() -> Path:
    override = os.environ.get("PLAYLIST_MANAGER_CONFIG")
    if override:
        return Path(override)
    base = os.environ.get("XDG_CONFIG_HOME")
    root = Path(base) if base else Path.home() / ".config"
    return root / "playlist-manager" / "settings.json"


def load_settings() -> Settings:
    path = config_file()
    if not path.is_file():
        return Settings()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return Settings()
    if not isinstance(data, dict):
        return Settings()
    playlist_dir = data.get("playlist_dir", "")
    vlc_command = data.get("vlc_command", "vlc")
    return Settings(
        playlist_dir=playlist_dir if isinstance(playlist_dir, str) else "",
        vlc_command=vlc_command.strip() if isinstance(vlc_command, str) and vlc_command.strip() else "vlc",
        column_widths=_column_widths(data.get("column_widths")),
    )


def save_settings(settings: Settings) -> None:
    path = config_file()
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "playlist_dir": settings.playlist_dir,
        "vlc_command": settings.vlc_command.strip() or "vlc",
        "column_widths": _column_widths(settings.column_widths),
    }
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def _column_widths(value) -> list[int]:
    if isinstance(value, list) and len(value) == COLUMN_COUNT:
        widths = []
        for item in value:
            if isinstance(item, bool) or not isinstance(item, int) or item < 24:
                break
            widths.append(item)
        else:
            return widths
    return list(DEFAULT_COLUMN_WIDTHS)
