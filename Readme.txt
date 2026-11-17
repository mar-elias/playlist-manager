# Mar-Elias Byzantine Hymns:

Edit https://github.com/GNOME/rhythmbox

Cloned at:
b496406df64a77a4a7a24e9af13981a909413b20

# Build
cd rhythmbox
meson setup _build -Dprefix=$PWD/_install
ninja -C _build
