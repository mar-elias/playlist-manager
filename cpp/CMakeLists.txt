cmake_minimum_required(VERSION 3.16)

project(marelias-playlist-manager-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(PLAYLIST_CORE_SOURCES
    src/shlex.cpp
    src/settings.cpp
    src/playlists.cpp
    src/tags.cpp
)

add_executable(playlist-manager
    src/main.cpp
    src/mainwindow.cpp
    resources.qrc
    ${PLAYLIST_CORE_SOURCES}
)

target_include_directories(playlist-manager PRIVATE src)
target_link_libraries(playlist-manager PRIVATE Qt6::Widgets)
target_compile_options(playlist-manager PRIVATE -Wall -Wextra -Wpedantic)
