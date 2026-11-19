#include "playlists.hpp"

#include "error.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <optional>

namespace {

const QSet<QString>& mediaSuffixes() {
    static const QSet<QString> suffixes = {
        QStringLiteral("3gp"),  QStringLiteral("aac"), QStringLiteral("aif"), QStringLiteral("aiff"),
        QStringLiteral("alac"), QStringLiteral("ape"), QStringLiteral("avi"), QStringLiteral("flac"),
        QStringLiteral("flv"),  QStringLiteral("m2ts"), QStringLiteral("m4a"), QStringLiteral("m4v"),
        QStringLiteral("mkv"),  QStringLiteral("mov"), QStringLiteral("mp2"), QStringLiteral("mp3"),
        QStringLiteral("mp4"),  QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("mts"),
        QStringLiteral("oga"),  QStringLiteral("ogg"), QStringLiteral("ogv"), QStringLiteral("opus"),
        QStringLiteral("ts"),   QStringLiteral("wav"), QStringLiteral("webm"), QStringLiteral("wma"),
        QStringLiteral("wmv"),  QStringLiteral("wv"),
    };
    return suffixes;
}

const QSet<QString>& videoSuffixes() {
    static const QSet<QString> suffixes = {
        QStringLiteral("3gp"), QStringLiteral("avi"), QStringLiteral("flv"), QStringLiteral("m2ts"),
        QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("mp4"),
        QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("mts"), QStringLiteral("ogv"),
        QStringLiteral("ts"), QStringLiteral("webm"), QStringLiteral("wmv"),
    };
    return suffixes;
}

QString suffixOf(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

[[noreturn]] void ioError(const QFileDevice& file, const QString& path) {
    throw AppError(QStringLiteral("%1: %2").arg(file.errorString(), path));
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        ioError(file, path);
    QByteArray data = file.readAll();
    if (data.startsWith("\xEF\xBB\xBF"))
        data = data.mid(3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(data);
    if (!decoder.hasError())
        return text;
    return QString::fromLatin1(data);
}

QStringList splitLines(const QString& text) {
    QStringList lines;
    int start = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character == '\n' || character == '\r') {
            lines.append(text.mid(start, index - start));
            if (character == '\r' && index + 1 < text.size() && text.at(index + 1) == '\n')
                ++index;
            start = index + 1;
        }
    }
    if (start < text.size())
        lines.append(text.mid(start));
    return lines;
}

QString unquoteValue(const QString& value) {
    const QString text = value.trimmed();
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        return text.mid(1, text.size() - 2);
    return text;
}

std::optional<QString> normalizeLocation(const QString& raw, const QString& base) {
    const QString text = raw.trimmed();
    if (text.isEmpty() || text.startsWith('#'))
        return std::nullopt;
    if (text.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(text);
        const QString host = url.host(QUrl::FullyDecoded);
        QString path = QDir::fromNativeSeparators(url.path(QUrl::FullyDecoded));
        if (!host.isEmpty() && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0)
            path = QStringLiteral("//") + host + path;
        return QDir::cleanPath(path);
    }
    if (text.contains(QStringLiteral("://")))
        return std::nullopt;
    QString path = QDir::fromNativeSeparators(text);
    if (QFileInfo(path).isRelative())
        path = QDir(base).filePath(path);
    return QDir::cleanPath(path);
}

QStringList loadM3u(const QString& path) {
    QStringList tracks;
    const QString base = QFileInfo(path).absolutePath();
    for (const QString& line : splitLines(readText(path))) {
        const std::optional<QString> location = normalizeLocation(line, base);
        if (location.has_value())
            tracks.append(*location);
    }
    return tracks;
}

QStringList loadPls(const QString& path) {
    QList<QPair<int, QString>> numbered;
    QStringList trailing;
    const QString base = QFileInfo(path).absolutePath();
    for (const QString& line : splitLines(readText(path))) {
        const int split = line.indexOf('=');
        if (split < 0)
            continue;
        const QString key = line.left(split).trimmed().toLower();
        if (!key.startsWith(QStringLiteral("file")))
            continue;
        const std::optional<QString> location = normalizeLocation(unquoteValue(line.mid(split + 1)), base);
        if (!location.has_value())
            continue;
        const QString suffix = key.mid(4);
        bool digits = !suffix.isEmpty();
        for (const QChar character : suffix) {
            if (!character.isDigit()) {
                digits = false;
                break;
            }
        }
        if (digits)
            numbered.append({suffix.toInt(), *location});
        else
            trailing.append(*location);
    }
    std::stable_sort(numbered.begin(), numbered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    QStringList tracks;
    for (const auto& item : numbered)
        tracks.append(item.second);
    tracks += trailing;
    return tracks;
}

QString localName(const QString& name) {
    const int split = name.lastIndexOf('}');
    return split < 0 ? name : name.mid(split + 1);
}

QStringList loadXspf(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        ioError(file, path);
    QXmlStreamReader reader(&file);
    QStringList tracks;
    const QString base = QFileInfo(path).absolutePath();
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || localName(reader.name().toString()) != QStringLiteral("location"))
            continue;
        const std::optional<QString> location = normalizeLocation(reader.readElementText(), base);
        if (location.has_value())
            tracks.append(*location);
    }
    if (reader.hasError()) {
        throw AppError(QStringLiteral("Could not read %1: %2")
                           .arg(QFileInfo(path).fileName(), reader.errorString()));
    }
    return tracks;
}

QString formatM3u8(const QStringList& tracks) {
    QString text = QStringLiteral("#EXTM3U\n");
    for (const QString& track : tracks) {
        if (track.contains('\n') || track.contains('\r'))
            throw AppError(QStringLiteral("A track path cannot contain a line break."));
        text += QStringLiteral("#EXTINF:-1,%1\n%2\n").arg(QFileInfo(track).fileName(), track);
    }
    return text;
}

QString uniquePath(const QString& directory, const QString& filename) {
    QDir folder(directory);
    QString candidate = folder.filePath(filename);
    if (!QFileInfo::exists(candidate))
        return candidate;
    const QString stem = QFileInfo(filename).completeBaseName();
    for (int number = 2;; ++number) {
        candidate = folder.filePath(QStringLiteral("%1 (%2).m3u8").arg(stem).arg(number));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

QString absoluteFile(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

}  // namespace

QString filenameForName(const QString& name) {
    QString cleaned = name.trimmed();
    if (cleaned.toLower().endsWith(QStringLiteral(".m3u8"))) {
        cleaned.chop(5);
        int end = cleaned.size();
        while (end > 0 && cleaned.at(end - 1).isSpace())
            --end;
        cleaned.truncate(end);
    }
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".") || cleaned == QStringLiteral("..")
        || cleaned.contains('/') || cleaned.contains('\\') || cleaned.contains(QChar('\0'))) {
        throw AppError(QStringLiteral("Enter a playlist name that does not contain / or \\."));
    }
    return cleaned + QStringLiteral(".m3u8");
}

QStringList listPlaylists(const QString& directory) {
    const QFileInfo folderInfo(directory);
    if (!folderInfo.isDir())
        throw AppError(QStringLiteral("Could not read the playlist folder."));
    QDir folder(directory);
    const QFileInfoList entries = folder.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    if (entries.isEmpty() && !folderInfo.isReadable())
        throw AppError(QStringLiteral("Permission denied: %1").arg(directory));
    QStringList paths;
    for (const QFileInfo& info : entries) {
        if (!info.isFile())
            continue;
        if (info.suffix().toLower() != QStringLiteral("m3u8"))
            continue;
        if (info.fileName().endsWith(QStringLiteral(".tmp")))
            continue;
        paths.append(absoluteFile(info.absoluteFilePath()));
    }
    std::stable_sort(paths.begin(), paths.end(), [](const QString& left, const QString& right) {
        return QFileInfo(left).completeBaseName().toCaseFolded()
            < QFileInfo(right).completeBaseName().toCaseFolded();
    });
    return paths;
}

QStringList loadTracks(const QString& path) {
    const QString suffix = suffixOf(path);
    if (suffix == QStringLiteral("m3u") || suffix == QStringLiteral("m3u8"))
        return loadM3u(path);
    if (suffix == QStringLiteral("pls"))
        return loadPls(path);
    if (suffix == QStringLiteral("xspf"))
        return loadXspf(path);
    throw AppError(QStringLiteral("Unsupported playlist format: %1").arg(QFileInfo(path).fileName()));
}

void saveTracks(const QString& path, const QStringList& tracks) {
    const QFileInfo info(path);
    if (!info.dir().exists() && !QDir().mkpath(info.absolutePath()))
        throw AppError(QStringLiteral("Could not create the playlist folder."));
    const QString text = formatM3u8(tracks);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        ioError(file, path);
    const QByteArray encoded = text.toUtf8();
    if (file.write(encoded) != encoded.size())
        ioError(file, path);
    if (!file.commit())
        ioError(file, path);
}

QString createPlaylist(const QString& directory, const QString& name) {
    const QString path = QDir(directory).filePath(filenameForName(name));
    if (QFileInfo::exists(path))
        throw AppError(QStringLiteral("A playlist with that name already exists."));
    saveTracks(path, {});
    return absoluteFile(path);
}

QString renamePlaylist(const QString& path, const QString& name) {
    const QString destination = QFileInfo(path).dir().filePath(filenameForName(name));
    if (destination != path && QFileInfo::exists(destination))
        throw AppError(QStringLiteral("A playlist with that name already exists."));
    if (destination != path) {
        QFile file(path);
        if (!file.rename(destination))
            ioError(file, path);
    }
    return absoluteFile(destination);
}

void deletePlaylist(const QString& path) {
    QFile file(path);
    if (!file.remove())
        ioError(file, path);
}

QString importPlaylist(const QString& source, const QString& directory) {
    const QStringList tracks = loadTracks(source);
    QString stem = QFileInfo(source).completeBaseName();
    if (stem.isEmpty())
        stem = QStringLiteral("Imported");
    const QString destination = uniquePath(directory, filenameForName(stem));
    saveTracks(destination, tracks);
    return absoluteFile(destination);
}

void exportPlaylist(const QString& source, const QString& destination) {
    const QStringList tracks = loadTracks(source);
    QString path = destination;
    if (!path.toLower().endsWith(QStringLiteral(".m3u8"))) {
        const QFileInfo info(path);
        path = info.dir().filePath(info.completeBaseName() + QStringLiteral(".m3u8"));
    }
    saveTracks(path, tracks);
}

bool hasMediaSuffix(const QString& path) {
    return mediaSuffixes().contains(suffixOf(path));
}

bool isVideoFile(const QString& path) {
    const QString suffix = suffixOf(path);
    if (videoSuffixes().contains(suffix))
        return true;
    if (mediaSuffixes().contains(suffix))
        return false;
    return QMimeDatabase().mimeTypeForFile(path).name().startsWith(QStringLiteral("video/"));
}

bool isMediaFile(const QString& path) {
    if (!QFileInfo(path).isFile())
        return false;
    if (hasMediaSuffix(path))
        return true;
    const QString name = QMimeDatabase().mimeTypeForFile(path).name();
    return name.startsWith(QStringLiteral("audio/")) || name.startsWith(QStringLiteral("video/"));
}
