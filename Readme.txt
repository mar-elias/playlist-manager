# Mar-Elias Byzantine Hymns:

Edit https://github.com/GNOME/rhythmbox

Cloned at:
b496406df64a77a4a7a24e9af13981a909413b20

# Build
cd rhythmbox
meson setup _build -Dprefix=$PWD/_install
ninja -C _build
CC=/usr/bin/clang ninja -C _build install
export GSETTINGS_SCHEMA_DIR="$PWD/_install/share/glib-2.0/schemas"
./_install/bin/rhythmbox

# Required for playing mp3/mp4:
sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
