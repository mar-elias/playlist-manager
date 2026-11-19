#pragma once

#include <QString>

#include <optional>

struct TrackInfo {
    QString title;
    QString artist;
    QString album;
    std::optional<int> year;
    QString genre;
    std::optional<double> duration;

    QString yearText() const { return year.has_value() ? QString::number(*year) : QString(); }
};

TrackInfo readTrackInfo(const QString& path);
QString formatDuration(std::optional<double> seconds);
std::optional<int> yearFromText(const QString& text);
