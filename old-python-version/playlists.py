"""M3U8 playlist files: load, save, import, and export."""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote, urlparse

MEDIA_SUFFIXES = frozenset(
    {
        ".3gp",
        ".aac",
        ".aif",
        ".aiff",
        ".alac",
        ".ape",
        ".avi",
        ".flac",
        ".flv",
        ".m2ts",
        ".m4a",
        ".m4v",
        ".mkv",
        ".mov",
        ".mp2",
        ".mp3",
        ".mp4",
        ".mpeg",
        ".mpg",
        ".mts",
        ".oga",
        ".ogg",
        ".ogv",
        ".opus",
        ".ts",
        ".wav",
        ".webm",
        ".wma",
        ".wmv",
        ".wv",
    }
)


def has_media_suffix(path: Path) -> bool:
    return path.suffix.lower() in MEDIA_SUFFIXES


def list_playlists(directory: Path) -> list[Path]:
    paths = [
        path
        for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() == ".m3u8" and not path.name.endswith(".tmp")
    ]
    return sorted(paths, key=lambda path: path.stem.casefold())


def load_tracks(path: Path) -> list[str]:
    suffix = path.suffix.lower()
    if suffix in {".m3u", ".m3u8"}:
        return _load_m3u(path)
    if suffix == ".pls":
        return _load_pls(path)
    if suffix == ".xspf":
        return _load_xspf(path)
    raise ValueError(f"Unsupported playlist format: {path.name}")


def save_tracks(path: Path, tracks: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(_format_m3u8(tracks), encoding="utf-8")
    temporary.replace(path)


def create_playlist(directory: Path, name: str) -> Path:
    path = directory / filename_for_name(name)
    if path.exists():
        raise ValueError("A playlist with that name already exists.")
    save_tracks(path, [])
    return path


def rename_playlist(path: Path, name: str) -> Path:
    destination = path.with_name(filename_for_name(name))
    if destination != path and destination.exists():
        raise ValueError("A playlist with that name already exists.")
    if destination != path:
        path.rename(destination)
    return destination


def delete_playlist(path: Path) -> None:
    path.unlink()


def import_playlist(source: Path, directory: Path) -> Path:
    tracks = load_tracks(source)
    filename = filename_for_name(source.stem or "Imported")
    destination = unique_path(directory, filename)
    save_tracks(destination, tracks)
    return destination


def export_playlist(source: Path, destination: Path) -> None:
    tracks = load_tracks(source)
    if destination.suffix.lower() != ".m3u8":
        destination = destination.with_suffix(".m3u8")
    save_tracks(destination, tracks)


def filename_for_name(name: str) -> str:
    cleaned = name.strip()
    if cleaned.lower().endswith(".m3u8"):
        cleaned = cleaned[: -len(".m3u8")].rstrip()
    if (
        not cleaned
        or cleaned in {".", ".."}
        or "/" in cleaned
        or "\\" in cleaned
        or "\x00" in cleaned
    ):
        raise ValueError("Enter a playlist name that does not contain / or \\.")
    return f"{cleaned}.m3u8"


def unique_path(directory: Path, filename: str) -> Path:
    candidate = directory / filename
    if not candidate.exists():
        return candidate
    stem = Path(filename).stem
    number = 2
    while True:
        candidate = directory / f"{stem} ({number}).m3u8"
        if not candidate.exists():
            return candidate
        number += 1


def _format_m3u8(tracks: list[str]) -> str:
    lines = ["#EXTM3U"]
    for track in tracks:
        if "\n" in track or "\r" in track:
            raise ValueError("A track path cannot contain a line break.")
        lines.append(f"#EXTINF:-1,{Path(track).name}")
        lines.append(track)
    return "\n".join(lines) + "\n"


def _load_m3u(path: Path) -> list[str]:
    tracks = []
    for line in _read_text(path).splitlines():
        location = _normalize_location(line, path.parent)
        if location is not None:
            tracks.append(location)
    return tracks


def _load_pls(path: Path) -> list[str]:
    numbered: list[tuple[int, str]] = []
    trailing: list[str] = []
    for line in _read_text(path).splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip().lower()
        if not key.startswith("file"):
            continue
        location = _normalize_location(_unquote_value(value), path.parent)
        if location is None:
            continue
        suffix = key[4:]
        if suffix.isdigit():
            numbered.append((int(suffix), location))
        else:
            trailing.append(location)
    numbered.sort(key=lambda item: item[0])
    return [location for _, location in numbered] + trailing


def _load_xspf(path: Path) -> list[str]:
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise ValueError(f"Could not read {path.name}: {exc}") from exc
    tracks = []
    for element in root.iter():
        if _local_name(element.tag) != "location" or not element.text:
            continue
        location = _normalize_location(element.text, path.parent)
        if location is not None:
            tracks.append(location)
    return tracks


def _read_text(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "latin-1"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("latin-1")


def _unquote_value(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        return text[1:-1]
    return text


def _normalize_location(raw: str, base: Path) -> str | None:
    text = raw.strip()
    if not text or text.startswith("#"):
        return None
    lowered = text.lower()
    if lowered.startswith("file:"):
        parsed = urlparse(text)
        if parsed.netloc and parsed.netloc not in {"localhost", ""}:
            path = Path(f"//{parsed.netloc}{unquote(parsed.path)}")
        else:
            path = Path(unquote(parsed.path))
        return os.path.normpath(str(path))
    if "://" in text:
        return None
    path = Path(text)
    if not path.is_absolute():
        path = base / path
    return os.path.normpath(str(path))


def _local_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[-1]
    return tag
