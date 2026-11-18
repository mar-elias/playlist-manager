#!/usr/bin/python3
"""Playlist manager. Tracks open in an external VLC process."""

from __future__ import annotations

import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import mutagen
except ImportError:
    mutagen = None

try:
    from PyQt6.QtCore import QMimeDatabase, Qt, QTimer, QUrl, pyqtSignal
    from PyQt6.QtGui import QColor, QKeySequence
    from PyQt6.QtWidgets import (
        QAbstractItemView,
        QApplication,
        QDialog,
        QDialogButtonBox,
        QFileDialog,
        QFormLayout,
        QHBoxLayout,
        QHeaderView,
        QInputDialog,
        QLabel,
        QLineEdit,
        QListWidget,
        QListWidgetItem,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QSizePolicy,
        QSplitter,
        QStackedWidget,
        QStatusBar,
        QToolButton,
        QStyle,
        QToolBar,
        QTreeWidget,
        QTreeWidgetItem,
        QVBoxLayout,
        QWidget,
    )
except ImportError:
    sys.stderr.write(
        "Playlist Manager needs PyQt6 on the system Python.\n"
        "Run it with /usr/bin/python3 playlist-manager/app.py\n"
    )
    raise SystemExit(1)

from playlists import (
    create_playlist,
    delete_playlist,
    export_playlist,
    filename_for_name,
    has_media_suffix,
    import_playlist,
    list_playlists,
    load_tracks,
    rename_playlist,
    save_tracks,
)
from settings import Settings, load_settings, save_settings

_MIME_DB = QMimeDatabase()
ROLE_INDEX = Qt.ItemDataRole.UserRole
ROLE_PATH = Qt.ItemDataRole.UserRole + 1


@dataclass(frozen=True)
class TrackInfo:
    title: str
    artist: str
    album: str
    year: int | None
    genre: str
    duration: float | None

    def year_text(self) -> str:
        return "" if self.year is None else str(self.year)


def read_track_info(path: str) -> TrackInfo:
    file = Path(path)
    title = file.stem
    artist = album = genre = ""
    year = None
    duration = None
    if file.is_file() and mutagen is not None:
        try:
            audio = mutagen.File(path, easy=True)
        except Exception:
            audio = None
        if audio is not None:
            title = _tag(audio, "title", "TIT2") or title
            artist = _tag(audio, "artist", "TPE1")
            album = _tag(audio, "album", "TALB")
            year = _year(_tag(audio, "date", "year", "TDRC", "TYER"))
            genre = _tag(audio, "genre", "TCON")
            length = getattr(getattr(audio, "info", None), "length", None)
            if isinstance(length, (int, float)) and length >= 0:
                duration = float(length)
    return TrackInfo(title, artist, album, year, genre, duration)


def _tag(audio, *keys: str) -> str:
    for key in keys:
        try:
            value = audio.get(key)
        except Exception:
            value = None
        text = _frame_text(value)
        if text:
            return text
    return ""


def _frame_text(value) -> str:
    if not value:
        return ""
    if isinstance(value, (list, tuple)):
        for item in value:
            text = _frame_text(item)
            if text:
                return text
        return ""
    if hasattr(value, "text"):
        return _frame_text(value.text)
    return str(value).strip()


def _year(text: str) -> int | None:
    digits = ""
    for char in text:
        if char.isdigit():
            digits += char
            if len(digits) == 4:
                return int(digits)
        elif digits:
            break
    return None


def format_duration(seconds: float | None) -> str:
    if seconds is None or seconds < 0:
        return ""
    total = int(seconds + 0.5)
    minutes, secs = divmod(total, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes}:{secs:02d}"


def media_paths_from_urls(urls) -> list[str]:
    paths = []
    for url in urls:
        if not url.isLocalFile():
            continue
        raw = url.toLocalFile()
        if not raw:
            continue
        path = Path(raw)
        if _is_media(path):
            paths.append(str(path.expanduser().resolve(strict=False)))
    return paths


def _is_media(path: Path) -> bool:
    if not path.is_file():
        return False
    if has_media_suffix(path):
        return True
    name = _MIME_DB.mimeTypeForFile(str(path)).name()
    return name.startswith("audio/") or name.startswith("video/")


def _error_color(widget: QWidget) -> QColor:
    text = widget.palette().color(widget.foregroundRole())
    if text.lightness() > 128:
        return QColor("#ff8a80")
    return QColor("#b00020")


class PlaylistList(QListWidget):
    """Sidebar that accepts audio and video files dropped from Dolphin."""

    files_dropped = pyqtSignal(str, list)

    def __init__(self):
        super().__init__()
        self.setAcceptDrops(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DropOnly)
        self.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.setMinimumWidth(260)
        self.setStyleSheet("QListWidget { font-size: 12pt; } QListWidget::item { padding: 5px 6px; }")

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        event.ignore()

    def dragMoveEvent(self, event):
        self.dragEnterEvent(event)

    def dropEvent(self, event):
        if not event.mimeData().hasUrls():
            event.ignore()
            return
        item = self.itemAt(event.position().toPoint())
        playlist = item.data(Qt.ItemDataRole.UserRole) if item is not None else ""
        self.files_dropped.emit(playlist or "", list(event.mimeData().urls()))
        event.acceptProposedAction()


class TrackView(QTreeWidget):
    """Track table. Rows reorder inside the playlist, and Dolphin can drop files in."""

    files_dropped = pyqtSignal(list, int)
    moved = pyqtSignal(list, int)
    remove_requested = pyqtSignal()
    move_requested = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self.setColumnCount(9)
        self.setHeaderLabels(
            ["#", "Title", "Artist", "Album", "Year", "Genre", "Time", "File name", "Location"]
        )
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.setRootIsDecorated(False)
        self.setIndentation(0)
        self.setAlternatingRowColors(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop)
        self.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.setAcceptDrops(True)
        self.setDragEnabled(True)
        header = self.header()
        header.setStretchLastSection(False)
        header.setSectionsClickable(True)
        header.setSortIndicatorShown(True)
        for column in (0, 4, 6):
            header.setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        for column, width in ((2, 150), (3, 150), (5, 120), (7, 180), (8, 260)):
            header.setSectionResizeMode(column, QHeaderView.ResizeMode.Interactive)
            self.setColumnWidth(column, width)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Delete and event.modifiers() == Qt.KeyboardModifier.NoModifier:
            self.remove_requested.emit()
            return
        if event.modifiers() == Qt.KeyboardModifier.AltModifier and event.key() == Qt.Key.Key_Up:
            self.move_requested.emit(-1)
            return
        if event.modifiers() == Qt.KeyboardModifier.AltModifier and event.key() == Qt.Key.Key_Down:
            self.move_requested.emit(1)
            return
        super().keyPressEvent(event)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        super().dragEnterEvent(event)

    def dragMoveEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        super().dragMoveEvent(event)

    def dropEvent(self, event):
        if event.mimeData().hasUrls():
            index = self._playlist_insert_index(event.position().toPoint())
            self.files_dropped.emit(list(event.mimeData().urls()), -1 if index is None else index)
            event.acceptProposedAction()
            return
        indexes = self.playlist_indexes(self.selectedItems())
        if not indexes:
            event.ignore()
            return
        destination = self._playlist_insert_index(event.position().toPoint())
        self.moved.emit(indexes, -1 if destination is None else destination)
        event.accept()

    def playlist_indexes(self, items) -> list[int]:
        indexes = []
        for item in items:
            value = item.data(0, ROLE_INDEX)
            if isinstance(value, int):
                indexes.append(value)
        return sorted(set(indexes))

    def _playlist_insert_index(self, pos) -> int | None:
        """Playlist index to insert before. None means append to the playlist."""
        item = self.itemAt(pos)
        if item is None:
            return None
        playlist_index = item.data(0, ROLE_INDEX)
        if not isinstance(playlist_index, int):
            return None
        rect = self.visualItemRect(item)
        if pos.y() > rect.center().y():
            return playlist_index + 1
        return playlist_index


class FileDropPage(QWidget):
    files_dropped = pyqtSignal(list)

    def __init__(self, text: str):
        super().__init__()
        self.setAcceptDrops(True)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        layout = QVBoxLayout(self)
        self.label = QLabel(text)
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.label.setWordWrap(True)
        layout.addWidget(self.label)

    def set_text(self, text: str) -> None:
        self.label.setText(text)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        event.ignore()

    def dragMoveEvent(self, event):
        self.dragEnterEvent(event)

    def dropEvent(self, event):
        if not event.mimeData().hasUrls():
            event.ignore()
            return
        self.files_dropped.emit(list(event.mimeData().urls()))
        event.acceptProposedAction()


class SettingsDialog(QDialog):
    def __init__(self, window: "PlaylistWindow"):
        super().__init__(window)
        self.window = window
        self.setWindowTitle("Settings")
        self.setMinimumWidth(480)
        layout = QFormLayout(self)
        self.folder = QLineEdit()
        self.folder.setPlaceholderText("Paste a folder path")
        choose = QPushButton("Choose")
        choose.clicked.connect(self.choose_folder)
        folder_row = QHBoxLayout()
        folder_row.addWidget(self.folder, 1)
        folder_row.addWidget(choose)
        layout.addRow("Playlist folder", folder_row)
        self.vlc = QLineEdit()
        layout.addRow("VLC command", self.vlc)
        hint = QLabel("Selected tracks open with this command. Example: vlc")
        hint.setWordWrap(True)
        layout.addRow(hint)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)
        self.refresh()

    def refresh(self) -> None:
        self.folder.setText(self.window.settings.playlist_dir.strip())
        self.vlc.setText(self.window.settings.vlc_command)

    def choose_folder(self) -> None:
        current = self.folder.text().strip()
        if not current or not Path(current).is_dir():
            current = self.window.settings.playlist_dir.strip()
        path = QFileDialog.getExistingDirectory(self, "Playlist folder", current)
        if path:
            self.folder.setText(path)

    def accept(self) -> None:
        folder = self.folder.text().strip()
        if folder and not Path(folder).is_dir():
            QMessageBox.critical(
                self,
                "Settings",
                f"That playlist folder is not available:\n{folder}",
            )
            return
        self.window.settings.playlist_dir = folder
        self.window.settings.vlc_command = self.vlc.text().strip() or "vlc"
        try:
            save_settings(self.window.settings)
        except OSError as exc:
            self.window.show_error(f"Could not save settings.\n{exc}")
            return
        self.window.reload_playlists()
        super().accept()


class PlaylistWindow(QMainWindow):
    def __init__(self, settings: Settings):
        super().__init__()
        self.settings = settings
        self.current_playlist: Path | None = None
        self.tracks: list[str] = []
        self._loading = False
        self._settings_dialog: SettingsDialog | None = None
        self._tag_cache: dict[str, tuple[int, TrackInfo]] = {}
        self.sort_column = 0
        self.sort_descending = False

        self.setWindowTitle("Playlist Manager")
        self.resize(1180, 640)
        self._build_actions()
        self._build_toolbar()
        self.setStatusBar(QStatusBar())

        self.playlist_list = PlaylistList()
        self.playlist_list.itemSelectionChanged.connect(self.on_playlist_selected)
        self.playlist_list.files_dropped.connect(self.on_playlist_files)

        self.track_view = TrackView()
        self.track_view.itemDoubleClicked.connect(self.on_track_activated)
        self.track_view.itemSelectionChanged.connect(self.update_actions)
        self.track_view.files_dropped.connect(self.on_track_files)
        self.track_view.moved.connect(self.on_tracks_moved)
        self.track_view.remove_requested.connect(self.remove_selected_tracks)
        self.track_view.move_requested.connect(self.move_selected)
        self.track_view.header().sectionClicked.connect(self.on_column_sorted)

        self.empty_page = FileDropPage("Drag audio and video files from Dolphin into this playlist.")
        self.none_page = FileDropPage("Select a playlist, or create a new one.")
        self.empty_page.files_dropped.connect(lambda urls: self.on_track_files(urls, None))
        self.none_page.files_dropped.connect(lambda urls: self.on_track_files(urls, None))

        self.track_stack = QStackedWidget()
        self.track_stack.addWidget(self.track_view)
        self.track_stack.addWidget(self.empty_page)
        self.track_stack.addWidget(self.none_page)

        track_bar = QHBoxLayout()
        track_bar.addStretch(1)
        track_bar.addWidget(self._tool_button(self.move_up_action))
        track_bar.addWidget(self._tool_button(self.move_down_action))
        track_bar.addWidget(self._tool_button(self.remove_action))
        track_pane = QWidget()
        track_layout = QVBoxLayout(track_pane)
        track_layout.setContentsMargins(0, 0, 0, 0)
        track_layout.addLayout(track_bar)
        track_layout.addWidget(self.track_stack, 1)

        self.main_pane = QSplitter()
        self.main_pane.addWidget(self.playlist_list)
        self.main_pane.addWidget(track_pane)
        self.main_pane.setStretchFactor(1, 1)
        self.main_pane.setSizes([280, 900])

        self.setup_page = FileDropPage("Each playlist is an M3U8 file in the folder you choose.")
        choose = QPushButton("Choose folder")
        choose.clicked.connect(self.choose_folder)
        self.setup_page.layout().addWidget(choose, alignment=Qt.AlignmentFlag.AlignHCenter)

        self.main_stack = QStackedWidget()
        self.main_stack.addWidget(self.setup_page)
        self.main_stack.addWidget(self.main_pane)
        self.setCentralWidget(self.main_stack)
        self.reload_playlists()

    def _build_actions(self) -> None:
        style = self.style()
        self.new_action = self._action(
            "New",
            style.standardIcon(QStyle.StandardPixmap.SP_FileDialogNewFolder),
            QKeySequence.StandardKey.New,
            self.on_new_playlist,
        )
        self.import_action = self._action(
            "Import",
            style.standardIcon(QStyle.StandardPixmap.SP_DialogOpenButton),
            QKeySequence("Ctrl+I"),
            self.on_import_playlist,
        )
        self.export_action = self._action(
            "Export",
            style.standardIcon(QStyle.StandardPixmap.SP_DialogSaveButton),
            QKeySequence("Ctrl+E"),
            self.on_export_playlist,
        )
        self.play_action = self._action(
            "Play",
            style.standardIcon(QStyle.StandardPixmap.SP_MediaPlay),
            QKeySequence("Ctrl+Return"),
            self.play,
        )
        self.rename_action = self._action("Rename", None, QKeySequence("F2"), self.on_rename_playlist)
        self.delete_action = self._action("Delete playlist", None, QKeySequence(), self.on_delete_playlist)
        self.remove_action = self._action(
            "Remove",
            style.standardIcon(QStyle.StandardPixmap.SP_TrashIcon),
            QKeySequence(),
            self.remove_selected_tracks,
        )
        self.move_up_action = self._action(
            "Move up",
            style.standardIcon(QStyle.StandardPixmap.SP_ArrowUp),
            QKeySequence(),
            lambda: self.move_selected(-1),
        )
        self.move_down_action = self._action(
            "Move down",
            style.standardIcon(QStyle.StandardPixmap.SP_ArrowDown),
            QKeySequence(),
            lambda: self.move_selected(1),
        )
        self.refresh_action = self._action(
            "Refresh",
            style.standardIcon(QStyle.StandardPixmap.SP_BrowserReload),
            QKeySequence.StandardKey.Refresh,
            self.refresh,
        )
        self.settings_action = self._action(
            "Settings",
            None,
            QKeySequence("Ctrl+,"),
            self.open_settings,
        )
        menu = self.menuBar()
        file_menu = menu.addMenu("File")
        file_menu.addAction(self.new_action)
        file_menu.addAction(self.import_action)
        file_menu.addAction(self.export_action)
        file_menu.addSeparator()
        file_menu.addAction("Quit", QKeySequence.StandardKey.Quit, self.close)
        playlist_menu = menu.addMenu("Playlist")
        playlist_menu.addAction(self.play_action)
        playlist_menu.addAction(self.rename_action)
        playlist_menu.addAction(self.delete_action)
        playlist_menu.addAction(self.remove_action)
        playlist_menu.addAction(self.move_up_action)
        playlist_menu.addAction(self.move_down_action)
        playlist_menu.addSeparator()
        playlist_menu.addAction(self.refresh_action)
        settings_menu = menu.addMenu("Settings")
        settings_menu.addAction(self.settings_action)

    def _action(self, text, icon, shortcut, slot):
        action = self.addAction(text)
        if icon is not None and not icon.isNull():
            action.setIcon(icon)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(slot)
        return action

    def _build_toolbar(self) -> None:
        toolbar = QToolBar()
        toolbar.setMovable(False)
        toolbar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        for action in (self.new_action, self.import_action, self.export_action, self.play_action):
            toolbar.addAction(action)
        self.addToolBar(toolbar)

    def _tool_button(self, action) -> QToolButton:
        button = QToolButton()
        button.setDefaultAction(action)
        return button

    def playlist_directory(self) -> Path | None:
        text = self.settings.playlist_dir.strip()
        if not text:
            return None
        directory = Path(text)
        if not directory.is_dir():
            return None
        return directory

    def reload_playlists(self, select: Path | None = None) -> None:
        if select is None:
            select = self.current_playlist
        directory = self.playlist_directory()
        self._loading = True
        self.playlist_list.clear()
        if directory is None:
            self._show_setup()
            self._loading = False
            self.current_playlist = None
            self.tracks = []
            self.show_tracks()
            return

        self.main_stack.setCurrentWidget(self.main_pane)
        try:
            playlists = list_playlists(directory)
        except OSError as exc:
            self._loading = False
            self.show_error(f"Could not read the playlist folder.\n{exc}")
            self.current_playlist = None
            self.tracks = []
            self.show_tracks()
            return

        chosen = -1
        for path in playlists:
            item = QListWidgetItem(path.name)
            item.setData(Qt.ItemDataRole.UserRole, str(path))
            self.playlist_list.addItem(item)
            if select is not None and path == select:
                chosen = self.playlist_list.count() - 1
        self._loading = False
        if chosen < 0 and self.playlist_list.count():
            chosen = 0
        if chosen >= 0:
            self.playlist_list.setCurrentRow(chosen)
        else:
            self.current_playlist = None
            self.tracks = []
            self.show_tracks()

    def _show_setup(self) -> None:
        text = self.settings.playlist_dir.strip()
        if text:
            message = f"The playlist folder is not available:\n{text}"
        else:
            message = "Each playlist is an M3U8 file in the folder you choose."
        self.setup_page.set_text(message)
        self.main_stack.setCurrentWidget(self.setup_page)

    def refresh(self) -> None:
        self.reload_playlists(select=self.current_playlist)

    def _track_info(self, path: str) -> TrackInfo:
        file = Path(path)
        try:
            stamp = file.stat().st_mtime_ns if file.exists() else -1
        except OSError:
            stamp = -1
        cached = self._tag_cache.get(path)
        if cached is not None and cached[0] == stamp:
            return cached[1]
        info = read_track_info(path)
        self._tag_cache[path] = (stamp, info)
        return info

    def _sort_value(self, index: int) -> tuple[bool, object]:
        info = self._track_info(self.tracks[index])
        path = Path(self.tracks[index])
        column = self.sort_column
        if column == 0:
            return False, index
        if column == 1:
            return info.title == "", info.title.casefold()
        if column == 2:
            return info.artist == "", info.artist.casefold()
        if column == 3:
            return info.album == "", info.album.casefold()
        if column == 4:
            return info.year is None, info.year or 0
        if column == 5:
            return info.genre == "", info.genre.casefold()
        if column == 6:
            return info.duration is None, info.duration or 0
        if column == 7:
            return path.name == "", path.name.casefold()
        location = "" if str(path.parent) in {"", "."} else str(path.parent)
        return location == "", location.casefold()

    def _display_order(self) -> list[int]:
        indexes = list(range(len(self.tracks)))
        indexes.sort(key=self._sort_value)
        if not self.sort_descending:
            return indexes
        present = [index for index in indexes if not self._sort_value(index)[0]]
        absent = [index for index in indexes if self._sort_value(index)[0]]
        present.reverse()
        return present + absent

    def show_tracks(self, select: list[int] | None = None) -> None:
        self.track_view.clear()
        missing_color = _error_color(self.track_view)
        chosen = set(select or [])
        for playlist_index in self._display_order():
            track = self.tracks[playlist_index]
            info = self._track_info(track)
            path = Path(track)
            missing = not path.exists()
            location = "" if str(path.parent) in {"", "."} else str(path.parent)
            item = QTreeWidgetItem(
                [
                    str(playlist_index + 1),
                    info.title,
                    info.artist,
                    info.album,
                    info.year_text(),
                    info.genre,
                    format_duration(info.duration),
                    path.name,
                    location,
                ]
            )
            item.setData(0, ROLE_INDEX, playlist_index)
            item.setData(0, ROLE_PATH, track)
            item.setTextAlignment(0, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            item.setTextAlignment(4, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            item.setTextAlignment(6, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            for column in range(9):
                item.setToolTip(column, track)
            if missing:
                for column in range(9):
                    item.setForeground(column, missing_color)
            self.track_view.addTopLevelItem(item)
            if playlist_index in chosen:
                item.setSelected(True)
        header = self.track_view.header()
        header.setSortIndicator(
            self.sort_column,
            Qt.SortOrder.DescendingOrder if self.sort_descending else Qt.SortOrder.AscendingOrder,
        )
        if self.current_playlist is None:
            self.track_stack.setCurrentWidget(self.none_page)
            self.setWindowTitle("Playlist Manager")
        else:
            self.setWindowTitle(f"{self.current_playlist.stem} — Playlist Manager")
            page = self.track_view if self.tracks else self.empty_page
            self.track_stack.setCurrentWidget(page)
        self.update_actions()

    def update_actions(self) -> None:
        has_dir = self.playlist_directory() is not None
        has_playlist = self.current_playlist is not None
        has_tracks = bool(self.selected_rows())
        self.new_action.setEnabled(has_dir)
        self.rename_action.setEnabled(has_playlist)
        self.delete_action.setEnabled(has_playlist)
        self.import_action.setEnabled(has_dir)
        self.export_action.setEnabled(has_playlist)
        self.remove_action.setEnabled(has_tracks)
        self.move_up_action.setEnabled(has_tracks)
        self.move_down_action.setEnabled(has_tracks)
        self.play_action.setEnabled(has_playlist)

    def on_playlist_selected(self) -> None:
        if self._loading:
            return
        item = self.playlist_list.currentItem()
        if item is None:
            self.current_playlist = None
            self.tracks = []
            self.show_tracks()
            return
        self.current_playlist = Path(item.data(Qt.ItemDataRole.UserRole))
        try:
            self.tracks = load_tracks(self.current_playlist)
        except (OSError, ValueError) as exc:
            self.tracks = []
            self.show_error(str(exc))
        self.show_tracks()

    def on_column_sorted(self, column: int) -> None:
        if column == self.sort_column:
            self.sort_descending = not self.sort_descending
        else:
            self.sort_column = column
            self.sort_descending = False
        self.show_tracks(select=self.selected_rows())

    def on_track_activated(self, item: QTreeWidgetItem, _column: int) -> None:
        path = item.data(0, ROLE_PATH)
        if path:
            self.play_paths([path])

    def selected_rows(self) -> list[int]:
        return self.track_view.playlist_indexes(self.track_view.selectedItems())

    def on_new_playlist(self) -> None:
        if self.playlist_directory() is None:
            self.toast("Choose a playlist folder first.")
            return
        name, accepted = QInputDialog.getText(
            self,
            "New playlist",
            "This becomes the playlist file name.",
        )
        if accepted:
            self._create_named_playlist(name)

    def _create_named_playlist(self, name: str) -> None:
        directory = self.playlist_directory()
        if directory is None:
            return
        try:
            path = create_playlist(directory, name)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))
            return
        self.reload_playlists(select=path)

    def on_rename_playlist(self) -> None:
        if self.current_playlist is None:
            return
        name, accepted = QInputDialog.getText(
            self,
            "Rename playlist",
            "This becomes the playlist file name.",
            text=self.current_playlist.stem,
        )
        if accepted:
            self._rename_current(name)

    def _rename_current(self, name: str) -> None:
        if self.current_playlist is None:
            return
        try:
            path = rename_playlist(self.current_playlist, name)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))
            return
        self.current_playlist = path
        self.reload_playlists(select=path)

    def on_delete_playlist(self) -> None:
        if self.current_playlist is None:
            return
        name = self.current_playlist.stem
        answer = QMessageBox.question(
            self,
            "Delete playlist?",
            f"“{name}” will be removed. The audio and video files stay where they are.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            delete_playlist(self.current_playlist)
        except OSError as exc:
            self.show_error(f"Could not delete the playlist.\n{exc}")
            return
        self.current_playlist = None
        self.reload_playlists(select=None)

    def on_import_playlist(self) -> None:
        directory = self.playlist_directory()
        if directory is None:
            self.toast("Choose a playlist folder first.")
            return
        source, _selected = QFileDialog.getOpenFileName(
            self,
            "Import playlist",
            str(directory),
            "Playlists (*.m3u8 *.m3u *.pls *.xspf)",
        )
        if not source:
            return
        try:
            path = import_playlist(Path(source), directory)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))
            return
        self.reload_playlists(select=path)

    def on_export_playlist(self) -> None:
        if self.current_playlist is None:
            return
        directory = self.playlist_directory()
        initial = str(directory / filename_for_name(self.current_playlist.stem)) if directory else ""
        destination, _selected = QFileDialog.getSaveFileName(
            self,
            "Export playlist",
            initial,
            "M3U8 playlist (*.m3u8)",
        )
        if not destination:
            return
        try:
            export_playlist(self.current_playlist, Path(destination))
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))

    def remove_selected_tracks(self) -> None:
        rows = self.selected_rows()
        if not rows or self.current_playlist is None:
            return
        if len(rows) == 1:
            name = self._track_info(self.tracks[rows[0]]).title or Path(self.tracks[rows[0]]).name
            question = f"Remove “{name}” from this playlist?"
        else:
            question = f"Remove {len(rows)} items from this playlist?"
        answer = QMessageBox.question(
            self,
            "Remove from playlist?",
            f"{question}\nThe files stay where they are.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        indexes = set(rows)
        self.tracks = [track for index, track in enumerate(self.tracks) if index not in indexes]
        self._write_current()

    def move_selected(self, delta: int) -> None:
        rows = self.selected_rows()
        if not rows or self.current_playlist is None or delta == 0:
            return
        if delta < 0 and rows[0] == 0:
            return
        if delta > 0 and rows[-1] == len(self.tracks) - 1:
            return
        order = rows if delta < 0 else list(reversed(rows))
        for index in order:
            swap = index + delta
            self.tracks[index], self.tracks[swap] = self.tracks[swap], self.tracks[index]
        self._write_current(select=[index + delta for index in rows])

    def on_tracks_moved(self, indexes: list[int], destination: int) -> None:
        copied = list(indexes)
        QTimer.singleShot(0, lambda copied=copied, destination=destination: self._move_indexes(copied, destination))

    def _move_indexes(self, indexes: list[int], destination: int) -> None:
        if self.current_playlist is None or not indexes:
            return
        indexes = sorted(set(indexes))
        if destination < 0:
            destination = len(self.tracks)
        moving = [self.tracks[index] for index in indexes]
        kept = [track for index, track in enumerate(self.tracks) if index not in set(indexes)]
        insert_at = destination - sum(1 for index in indexes if index < destination)
        insert_at = max(0, min(insert_at, len(kept)))
        updated = kept[:insert_at] + moving + kept[insert_at:]
        if updated == self.tracks:
            return
        self.tracks = updated
        self._write_current(select=list(range(insert_at, insert_at + len(moving))))

    def on_playlist_files(self, playlist: str, urls: list) -> None:
        path = Path(playlist) if playlist else self.current_playlist
        if path is None:
            self.toast("Create a playlist first.")
            return
        media = media_paths_from_urls(urls)
        if not media:
            self.toast("No audio or video files in that drop.")
            return
        QTimer.singleShot(0, lambda path=path, media=media: self._finish_playlist_drop(path, media))

    def _finish_playlist_drop(self, playlist: Path, paths: list[str]) -> None:
        try:
            self._add_paths(playlist, paths, None)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))
            return
        if self.current_playlist != playlist:
            self._select_playlist(playlist)

    def on_track_files(self, urls: list, index: int | None) -> None:
        if self.current_playlist is None:
            self.toast("Create a playlist first.")
            return
        media = media_paths_from_urls(urls)
        if not media:
            self.toast("No audio or video files in that drop.")
            return
        playlist = self.current_playlist
        insert_at = None if index is None or index < 0 else index
        QTimer.singleShot(
            0,
            lambda playlist=playlist, media=media, insert_at=insert_at: self._finish_track_drop(
                playlist, media, insert_at
            ),
        )

    def _finish_track_drop(self, playlist: Path, paths: list[str], index: int | None) -> None:
        try:
            self._add_paths(playlist, paths, index)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))

    def _select_playlist(self, playlist: Path) -> None:
        for row in range(self.playlist_list.count()):
            item = self.playlist_list.item(row)
            if Path(item.data(Qt.ItemDataRole.UserRole)) == playlist:
                self.playlist_list.setCurrentRow(row)
                return

    def _add_paths(self, playlist: Path, paths: list[str], index: int | None) -> None:
        tracks = load_tracks(playlist)
        insert_at = len(tracks) if index is None else max(0, min(index, len(tracks)))
        for offset, path in enumerate(paths):
            tracks.insert(insert_at + offset, path)
        save_tracks(playlist, tracks)
        if self.current_playlist == playlist:
            self.tracks = tracks
            self.show_tracks(select=list(range(insert_at, insert_at + len(paths))))

    def _write_current(self, select: list[int] | None = None) -> None:
        if self.current_playlist is None:
            return
        try:
            save_tracks(self.current_playlist, self.tracks)
        except (OSError, ValueError) as exc:
            self.show_error(str(exc))
            return
        self.show_tracks(select=select)

    def play(self) -> None:
        rows = self.selected_rows()
        if rows:
            self.play_paths([self.tracks[index] for index in rows])
            return
        if self.current_playlist is not None:
            self.play_paths([str(self.current_playlist)])

    def play_paths(self, paths: list[str]) -> None:
        if not paths:
            return
        raw = self.settings.vlc_command.strip() or "vlc"
        try:
            command = shlex.split(raw)
        except ValueError as exc:
            self.show_error(f"The VLC command could not be read.\n{exc}")
            return
        if not command:
            command = ["vlc"]
        try:
            subprocess.Popen(command + paths, start_new_session=True)
        except OSError as exc:
            self.show_error(f"Could not start VLC.\n{exc}")

    def open_settings(self) -> None:
        if self._settings_dialog is None:
            self._settings_dialog = SettingsDialog(self)
        else:
            self._settings_dialog.refresh()
        self._settings_dialog.show()
        self._settings_dialog.raise_()
        self._settings_dialog.activateWindow()

    def choose_folder(self) -> None:
        current = self.settings.playlist_dir.strip()
        path = QFileDialog.getExistingDirectory(self, "Playlist folder", current)
        if not path:
            return
        self.settings.playlist_dir = path
        try:
            save_settings(self.settings)
        except OSError as exc:
            self.show_error(f"Could not save settings.\n{exc}")
            return
        if self._settings_dialog is not None:
            self._settings_dialog.refresh()
        self.reload_playlists(select=self.current_playlist)

    def toast(self, message: str) -> None:
        self.statusBar().showMessage(message, 3000)

    def show_error(self, message: str) -> None:
        QMessageBox.critical(self, "Playlist Manager", message)


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Playlist Manager")
    window = PlaylistWindow(load_settings())
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
