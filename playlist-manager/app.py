#!/usr/bin/python3
"""Playlist manager. Tracks open in an external VLC process."""

from __future__ import annotations

import shlex
import subprocess
import sys
from pathlib import Path

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
        self.setMinimumWidth(220)

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
    reordered = pyqtSignal(list)
    remove_requested = pyqtSignal()
    move_requested = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self.setColumnCount(3)
        self.setHeaderLabels(["Name", "Location", ""])
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
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Interactive)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.setColumnWidth(0, 240)

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
            index = self._insert_index(event.position().toPoint())
            self.files_dropped.emit(list(event.mimeData().urls()), index)
            event.acceptProposedAction()
            return
        super().dropEvent(event)
        paths = []
        for row in range(self.topLevelItemCount()):
            paths.append(self.topLevelItem(row).data(0, Qt.ItemDataRole.UserRole))
        self.reordered.emit(paths)

    def _insert_index(self, pos) -> int:
        item = self.itemAt(pos)
        if item is None:
            return self.topLevelItemCount()
        index = self.indexOfTopLevelItem(item)
        rect = self.visualItemRect(item)
        if pos.y() > rect.center().y():
            return index + 1
        return index


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
        self.folder_label = QLabel()
        self.folder_label.setWordWrap(True)
        choose = QPushButton("Choose")
        choose.clicked.connect(self.choose_folder)
        folder_row = QHBoxLayout()
        folder_row.addWidget(self.folder_label, 1)
        folder_row.addWidget(choose)
        layout.addRow("Playlist folder", folder_row)
        self.vlc = QLineEdit()
        layout.addRow("VLC command", self.vlc)
        hint = QLabel("Selected tracks open with this command. Example: vlc")
        hint.setWordWrap(True)
        layout.addRow(hint)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(self.close)
        buttons.accepted.connect(self.close)
        layout.addRow(buttons)
        self.finished.connect(lambda _code: self._save_vlc())
        self.refresh()

    def refresh(self) -> None:
        self.folder_label.setText(self.window.settings.playlist_dir.strip() or "Not set")
        self.vlc.setText(self.window.settings.vlc_command)

    def choose_folder(self) -> None:
        current = self.window.settings.playlist_dir.strip()
        path = QFileDialog.getExistingDirectory(self, "Playlist folder", current)
        if not path:
            return
        self.window.settings.playlist_dir = path
        try:
            save_settings(self.window.settings)
        except OSError as exc:
            self.window.show_error(f"Could not save settings.\n{exc}")
            return
        self.folder_label.setText(path)
        self.window.reload_playlists()

    def _save_vlc(self) -> None:
        self.window.settings.vlc_command = self.vlc.text().strip() or "vlc"
        try:
            save_settings(self.window.settings)
        except OSError as exc:
            self.window.show_error(f"Could not save settings.\n{exc}")


class PlaylistWindow(QMainWindow):
    def __init__(self, settings: Settings):
        super().__init__()
        self.settings = settings
        self.current_playlist: Path | None = None
        self.tracks: list[str] = []
        self._loading = False
        self._settings_dialog: SettingsDialog | None = None

        self.setWindowTitle("Playlist Manager")
        self.resize(960, 640)
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
        self.track_view.reordered.connect(self.on_tracks_reordered)
        self.track_view.remove_requested.connect(self.remove_selected_tracks)
        self.track_view.move_requested.connect(self.move_selected)

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
        self.main_pane.setSizes([240, 720])

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
            item = QListWidgetItem(path.stem)
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

    def show_tracks(self, select: list[int] | None = None) -> None:
        self.track_view.clear()
        missing_color = _error_color(self.track_view)
        for track in self.tracks:
            missing = not Path(track).exists()
            item = QTreeWidgetItem([Path(track).name, track, "Missing" if missing else ""])
            item.setData(0, Qt.ItemDataRole.UserRole, track)
            item.setToolTip(0, track)
            item.setToolTip(1, track)
            if missing:
                for column in range(3):
                    item.setForeground(column, missing_color)
            self.track_view.addTopLevelItem(item)
        if select:
            for index in select:
                item = self.track_view.topLevelItem(index)
                if item is not None:
                    item.setSelected(True)
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

    def on_track_activated(self, item: QTreeWidgetItem, _column: int) -> None:
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if path:
            self.play_paths([path])

    def selected_rows(self) -> list[int]:
        rows = [self.track_view.indexOfTopLevelItem(item) for item in self.track_view.selectedItems()]
        return sorted(row for row in rows if row >= 0)

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

    def on_tracks_reordered(self, paths: list[str]) -> None:
        copied = list(paths)
        QTimer.singleShot(0, lambda copied=copied: self._apply_reorder(copied))

    def _apply_reorder(self, paths: list[str]) -> None:
        if self.current_playlist is None:
            return
        self.tracks = paths
        self._write_current()

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
        QTimer.singleShot(
            0,
            lambda playlist=playlist, media=media, index=index: self._finish_track_drop(playlist, media, index),
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
