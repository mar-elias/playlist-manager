#include "settings.hpp"

#include "error.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

namespace {

QList<int> defaultWidths() {
    QList<int> widths;
    widths.reserve(kColumnCount);
    for (int width : kDefaultColumnWidths)
        widths.append(width);
    return widths;
}

bool isWholeNumber(const QJsonValue& value, int& width) {
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    width = static_cast<int>(number);
    return number == static_cast<double>(width) && width >= 24;
}

}  // namespace

QList<int> normalizedColumnWidths(const QList<int>& widths) {
    if (widths.size() != kColumnCount)
        return defaultWidths();
    for (int width : widths) {
        if (width < 24)
            return defaultWidths();
    }
    return widths;
}

QString configFile() {
    const QString override = qEnvironmentVariable("PLAYLIST_MANAGER_CONFIG");
    if (!override.isEmpty())
        return override;
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return QDir(base).filePath(QStringLiteral("playlist-manager/settings.json"));
}

Settings loadSettings() {
    Settings settings;
    QFile file(configFile());
    if (!file.open(QIODevice::ReadOnly))
        return settings;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return settings;
    const QJsonObject object = document.object();
    const QJsonValue playlistDir = object.value(QStringLiteral("playlist_dir"));
    if (playlistDir.isString())
        settings.playlistDir = playlistDir.toString();
    const QJsonValue vlcCommand = object.value(QStringLiteral("vlc_command"));
    if (vlcCommand.isString()) {
        const QString command = vlcCommand.toString().trimmed();
        settings.vlcCommand = command.isEmpty() ? QStringLiteral("vlc") : command;
    }

    const QJsonValue widthsValue = object.value(QStringLiteral("column_widths"));
    if (widthsValue.isArray()) {
        const QJsonArray array = widthsValue.toArray();
        if (array.size() == kColumnCount) {
            QList<int> widths;
            bool valid = true;
            for (const QJsonValue& item : array) {
                int width = 0;
                if (!isWholeNumber(item, width)) {
                    valid = false;
                    break;
                }
                widths.append(width);
            }
            if (valid)
                settings.columnWidths = widths;
        }
    }
    return settings;
}

void saveSettings(const Settings& settings) {
    const QString path = configFile();
    QFileInfo info(path);
    if (!info.dir().exists() && !QDir().mkpath(info.absolutePath()))
        throw AppError(QStringLiteral("Could not create the settings folder."));

    QJsonArray widths;
    for (int width : normalizedColumnWidths(settings.columnWidths))
        widths.append(width);
    const QString command = settings.vlcCommand.trimmed().isEmpty()
        ? QStringLiteral("vlc")
        : settings.vlcCommand.trimmed();
    QJsonObject object;
    object.insert(QStringLiteral("playlist_dir"), settings.playlistDir);
    object.insert(QStringLiteral("vlc_command"), command);
    object.insert(QStringLiteral("column_widths"), widths);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw AppError(QStringLiteral("%1: %2").arg(file.errorString(), path));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
        throw AppError(QStringLiteral("%1: %2").arg(file.errorString(), path));
}
