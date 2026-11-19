#pragma once

#include <QString>
#include <QStringList>

QString filenameForName(const QString& name);
QStringList listPlaylists(const QString& directory);
QStringList loadTracks(const QString& path);
void saveTracks(const QString& path, const QStringList& tracks);
QString createPlaylist(const QString& directory, const QString& name);
QString renamePlaylist(const QString& path, const QString& name);
void deletePlaylist(const QString& path);
QString importPlaylist(const QString& source, const QString& directory);
void exportPlaylist(const QString& source, const QString& destination);

bool hasMediaSuffix(const QString& path);
bool isVideoFile(const QString& path);
bool isMediaFile(const QString& path);
