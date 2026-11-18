# Mar-Elias Byzantine Hymns:

A modified version of https://github.com/GNOME/rhythmbox

Cloned at: b496406df64a77a4a7a24e9af13981a909413b20

Can accept mp4 files. Trimmed down aggressively to be only a local music manager, nothing else.

# Build
meson setup _build -Dprefix=$PWD/_install
ninja -C _build
CC=/usr/bin/clang ninja -C _build install
export GSETTINGS_SCHEMA_DIR="$PWD/_install/share/glib-2.0/schemas"
./_install/bin/rhythmbox

# Required for playing mp3/mp4:
sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav

# TODO:
- For mp4 files, show a monitor next to files, open them in vlc
- Option to export/import playlists
X - remove all radio tabs
X - remove the rest of plugins
- restore recently played tab
- make sure tags are written to files
X - remove plugins tab in settings window
