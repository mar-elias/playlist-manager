#pragma once

#include <QList>
#include <QString>

inline constexpr int kColumnCount = 9;
inline constexpr int kDefaultColumnWidths[kColumnCount] = {56, 220, 150, 150, 72, 120, 72, 180, 260};

struct Settings {
    QString playlistDir;
    QString vlcCommand = QStringLiteral("vlc");
    QList<int> columnWidths = {
        kDefaultColumnWidths[0],
        kDefaultColumnWidths[1],
        kDefaultColumnWidths[2],
        kDefaultColumnWidths[3],
        kDefaultColumnWidths[4],
        kDefaultColumnWidths[5],
        kDefaultColumnWidths[6],
        kDefaultColumnWidths[7],
        kDefaultColumnWidths[8],
    };
};

QString configFile();
Settings loadSettings();
void saveSettings(const Settings& settings);
QList<int> normalizedColumnWidths(const QList<int>& widths);
