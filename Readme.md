# Mar-Elias Playlist-Manager
A minimal local playlist manager which uses VLC.

Playlists are M3U8 files saved in a folder chosen in Settings.

## Run
# Python version (moved to c++):
```
        /usr/bin/python3 app.py
```

## Information:
- Create, rename, and delete playlists. The sidebar lists every `.m3u8` file in the playlist folder.
- Drag audio and video files from Dolphin onto a playlist or into the track list. New files are added at the end. Move tracks up or down with the buttons to change their order.
- Import M3U, M3U8, PLS, and XSPF. Export writes an M3U8 file.
- Double-click a track to open it in VLC. Play opens the selected tracks, or the whole playlist when nothing is selected.
- Missing files are marked and are still kept in the playlist.
