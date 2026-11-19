# Mar-Elias Playlist Manager (C++)

The same local playlist manager as `app.py`, built with C++ and Qt 6. Playlists are M3U8 files. Tracks open in an external VLC process. Settings are stored in the same `playlist-manager/settings.json` file the Python app uses.

## Build

Qt 6 Widgets and a C++17 compiler are required. `ffprobe`, when it is on `PATH`, supplies tags and duration for formats the built-in reader does not cover.

```sh
cmake -S cpp -B cpp/build
cmake --build cpp/build
./cpp/build/playlist-manager
```

`playlist-manager-tests` checks playlist files, settings, and tag reading.
