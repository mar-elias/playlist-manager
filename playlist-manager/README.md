# Playlist Manager

A local playlist manager. It does not play media and it does not import a music library. Tracks open in VLC.

Playlists are M3U8 files in a folder chosen in Settings.

## Run

PyQt6 comes from the system Python, not from a conda environment:

```sh
/usr/bin/python3 playlist-manager/app.py
```

Package: `python3-pyqt6`. VLC should be on `PATH`, or set another command in Settings.

## Use

- Create, rename, and delete playlists. The sidebar lists every `.m3u8` file in the playlist folder.
- Drag audio and video files from Dolphin onto a playlist or into the track list. Drag a row to reorder it.
- Import M3U, M3U8, PLS, and XSPF. Export writes an M3U8 file.
- Double-click a track to open it in VLC. Play opens the selected tracks, or the whole playlist when nothing is selected.
- Missing files are marked and are still kept in the playlist.
