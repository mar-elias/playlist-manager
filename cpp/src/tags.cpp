#include "tags.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace {

constexpr int kMaxTagBytes = 16 * 1024 * 1024;

const char* kGenres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
    "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
    "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop",
    "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game",
    "Sound Clip", "Gospel", "Noise", "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
    "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial",
    "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
    "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave",
    "Psychadelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka",
    "Retro", "Musical", "Rock & Roll", "Hard Rock",
};

struct Fields {
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString yearText;
    std::optional<double> duration;
    std::optional<double> tlen;
    bool parsed = false;

    bool hasText() const {
        return !title.isEmpty() || !artist.isEmpty() || !album.isEmpty() || !genre.isEmpty()
            || !yearText.isEmpty();
    }

    void setTitle(const QString& value) {
        const QString text = value.trimmed();
        if (!text.isEmpty() && title.isEmpty())
            title = text;
    }
    void setArtist(const QString& value) {
        const QString text = value.trimmed();
        if (!text.isEmpty() && artist.isEmpty())
            artist = text;
    }
    void setAlbum(const QString& value) {
        const QString text = value.trimmed();
        if (!text.isEmpty() && album.isEmpty())
            album = text;
    }
    void setGenre(const QString& value) {
        const QString text = normalizeGenre(value);
        if (!text.isEmpty() && genre.isEmpty())
            genre = text;
    }
    void setYear(const QString& value) {
        if (yearText.isEmpty()) {
            const auto year = yearFromText(value);
            if (year.has_value())
                yearText = QString::number(*year);
        }
    }
    void setDuration(double seconds) {
        if (!duration.has_value() && seconds >= 0 && std::isfinite(seconds))
            duration = seconds;
    }

    static QString normalizeGenre(QString text);
};

QString genreName(int index) {
    constexpr int count = int(sizeof(kGenres) / sizeof(kGenres[0]));
    if (index < 0 || index >= count)
        return {};
    return QString::fromLatin1(kGenres[index]);
}

QString Fields::normalizeGenre(QString text) {
    text = text.trimmed();
    if (text.size() >= 3 && text.front() == '(' && text.contains(')')) {
        const int end = text.indexOf(')');
        const QString number = text.mid(1, end - 1);
        bool ok = false;
        const int index = number.toInt(&ok);
        const QString rest = text.mid(end + 1).trimmed();
        if (ok && number == QString::number(index)) {
            if (!rest.isEmpty())
                return rest;
            const QString named = genreName(index);
            return named.isEmpty() ? text : named;
        }
    }
    bool ok = false;
    const int index = text.toInt(&ok);
    if (ok && text == QString::number(index)) {
        const QString named = genreName(index);
        if (!named.isEmpty())
            return named;
    }
    return text;
}

int syncsafe(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 4 > data.size())
        return -1;
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return (int(bytes[0] & 0x7f) << 21) | (int(bytes[1] & 0x7f) << 14) | (int(bytes[2] & 0x7f) << 7)
        | int(bytes[3] & 0x7f);
}

quint32 be32(const QByteArray& data, int offset) {
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return (quint32(bytes[0]) << 24) | (quint32(bytes[1]) << 16) | (quint32(bytes[2]) << 8) | quint32(bytes[3]);
}

quint64 be64(const QByteArray& data, int offset) {
    quint64 value = 0;
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    for (int index = 0; index < 8; ++index)
        value = (value << 8) | bytes[index];
    return value;
}

quint32 le32(const QByteArray& data, int offset) {
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint32(bytes[0]) | (quint32(bytes[1]) << 8) | (quint32(bytes[2]) << 16) | (quint32(bytes[3]) << 24);
}

quint64 le64(const QByteArray& data, int offset) {
    quint64 value = 0;
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    for (int index = 7; index >= 0; --index)
        value = (value << 8) | bytes[index];
    return value;
}

quint16 le16(const QByteArray& data, int offset) {
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint16(bytes[0]) | (quint16(bytes[1]) << 8);
}

quint16 be16(const QByteArray& data, int offset) {
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return (quint16(bytes[0]) << 8) | quint16(bytes[1]);
}

QByteArray readAt(QFile& file, qint64 position, int length) {
    if (length <= 0 || length > kMaxTagBytes || !file.seek(position))
        return {};
    const QByteArray data = file.read(length);
    return data.size() == length ? data : QByteArray();
}

QByteArray deunsync(const QByteArray& input) {
    QByteArray output;
    output.reserve(input.size());
    for (int index = 0; index < input.size(); ++index) {
        output.append(input.at(index));
        if (uchar(input.at(index)) == 0xff && index + 1 < input.size() && input.at(index + 1) == '\0')
            ++index;
    }
    return output;
}

QString decodeUtf16(const QByteArray& raw) {
    if (raw.size() < 2)
        return {};
    int bom = 0;
    QStringConverter::Encoding encoding = QStringConverter::Utf16LE;
    if (uchar(raw.at(0)) == 0xff && uchar(raw.at(1)) == 0xfe) {
        encoding = QStringConverter::Utf16LE;
        bom = 2;
    } else if (uchar(raw.at(0)) == 0xfe && uchar(raw.at(1)) == 0xff) {
        encoding = QStringConverter::Utf16BE;
        bom = 2;
    }
    int end = bom;
    while (end + 1 < raw.size()) {
        if (raw.at(end) == '\0' && raw.at(end + 1) == '\0')
            break;
        end += 2;
    }
    QStringDecoder decoder(encoding);
    const QString text = decoder.decode(raw.mid(bom, end - bom));
    return text.trimmed();
}

QString decodeTextFrame(const QByteArray& frame) {
    if (frame.isEmpty())
        return {};
    const int encoding = uchar(frame.at(0));
    QByteArray raw = frame.mid(1);
    if (encoding == 0) {
        const int end = raw.indexOf('\0');
        if (end >= 0)
            raw.truncate(end);
        return QString::fromLatin1(raw).trimmed();
    }
    if (encoding == 1 || encoding == 2) {
        if (encoding == 2 && !(raw.size() >= 2 && uchar(raw.at(0)) == 0xfe)) {
            QByteArray swapped;
            swapped.resize(raw.size() - (raw.size() % 2));
            for (int index = 0; index + 1 < raw.size(); index += 2) {
                swapped[index] = raw.at(index + 1);
                swapped[index + 1] = raw.at(index);
            }
            return decodeUtf16(swapped);
        }
        return decodeUtf16(raw);
    }
    if (encoding == 3) {
        const int end = raw.indexOf('\0');
        if (end >= 0)
            raw.truncate(end);
        return QString::fromUtf8(raw).trimmed();
    }
    return {};
}

void applyText(Fields& fields, const QByteArray& id, const QString& text) {
    if (text.isEmpty())
        return;
    fields.parsed = true;
    if (id == "TIT2" || id == "TT2")
        fields.setTitle(text);
    else if (id == "TPE1" || id == "TP1")
        fields.setArtist(text);
    else if (id == "TALB" || id == "TAL")
        fields.setAlbum(text);
    else if (id == "TCON" || id == "TCO")
        fields.setGenre(text);
    else if (id == "TDRC" || id == "TYER" || id == "TYE")
        fields.setYear(text);
    else if (id == "TLEN" || id == "TLE") {
        bool ok = false;
        const double milliseconds = text.trimmed().toDouble(&ok);
        if (ok && milliseconds >= 0)
            fields.tlen = milliseconds / 1000.0;
    }
}

void parseId3v2(QFile& file, Fields& fields) {
    const QByteArray header = readAt(file, 0, 10);
    if (header.size() != 10 || !header.startsWith("ID3"))
        return;
    const int version = uchar(header.at(3));
    const int flags = uchar(header.at(5));
    if (version < 2 || version > 4)
        return;
    const int tagSize = syncsafe(header, 6);
    if (tagSize <= 0)
        return;
    QByteArray body = readAt(file, 10, tagSize);
    if (body.size() != tagSize)
        return;
    if (flags & 0x80)
        body = deunsync(body);
    int offset = 0;
    if (flags & 0x40) {
        const int extended = version == 3 ? (body.size() >= 4 ? int(be32(body, 0)) : -1) : syncsafe(body, 0);
        if (extended < 4 || extended > body.size())
            return;
        offset = extended;
    }

    while (offset < body.size()) {
        if (version == 2) {
            if (offset + 6 > body.size() || body.at(offset) == '\0')
                break;
            const QByteArray id = body.mid(offset, 3);
            const int size = (uchar(body.at(offset + 3)) << 16) | (uchar(body.at(offset + 4)) << 8)
                | uchar(body.at(offset + 5));
            offset += 6;
            if (size < 0 || offset + size > body.size())
                break;
            applyText(fields, id, decodeTextFrame(body.mid(offset, size)));
            offset += size;
            continue;
        }
        if (offset + 10 > body.size() || body.at(offset) == '\0')
            break;
        const QByteArray id = body.mid(offset, 4);
        const int size = version == 4 ? syncsafe(body, offset + 4) : int(be32(body, offset + 4));
        const int frameFlags = (uchar(body.at(offset + 8)) << 8) | uchar(body.at(offset + 9));
        offset += 10;
        if (size < 0 || offset + size > body.size())
            break;
        QByteArray frame = body.mid(offset, size);
        offset += size;
        if (version == 3) {
            if (frameFlags & 0x00c0)
                continue;
            if ((frameFlags & 0x0020) && !frame.isEmpty())
                frame = frame.mid(1);
        } else {
            if (frameFlags & 0x000c)
                continue;
            if ((frameFlags & 0x0040) && !frame.isEmpty())
                frame = frame.mid(1);
            if ((frameFlags & 0x0001) && frame.size() >= 4)
                frame = frame.mid(4);
            if (frameFlags & 0x0002)
                frame = deunsync(frame);
        }
        applyText(fields, id, decodeTextFrame(frame));
    }
}

void parseId3v1(QFile& file, Fields& fields) {
    if (file.size() < 128)
        return;
    const QByteArray tag = readAt(file, file.size() - 128, 128);
    if (tag.size() != 128 || !tag.startsWith("TAG"))
        return;
    auto latin = [](const QByteArray& data) {
        const int end = data.indexOf('\0');
        return QString::fromLatin1(end >= 0 ? data.left(end) : data).trimmed();
    };
    fields.setTitle(latin(tag.mid(3, 30)));
    fields.setArtist(latin(tag.mid(33, 30)));
    fields.setAlbum(latin(tag.mid(63, 30)));
    fields.setYear(latin(tag.mid(93, 4)));
    const int genre = uchar(tag.at(127));
    if (genre != 255)
        fields.setGenre(genreName(genre));
    if (fields.hasText())
        fields.parsed = true;
}

QString firstValue(const QByteArray& raw) {
    const int end = raw.indexOf('\0');
    const QByteArray slice = end >= 0 ? raw.left(end) : raw;
    const QString utf8 = QString::fromUtf8(slice).trimmed();
    if (!utf8.isEmpty() && !utf8.contains(QChar::ReplacementCharacter))
        return utf8;
    return QString::fromLatin1(slice).trimmed();
}

void applyVorbis(Fields& fields, const QByteArray& block) {
    if (block.size() < 8)
        return;
    int offset = 0;
    const quint32 vendorLength = le32(block, offset);
    offset += 4;
    if (vendorLength > quint32(block.size() - offset))
        return;
    offset += int(vendorLength);
    if (offset + 4 > block.size())
        return;
    const quint32 count = le32(block, offset);
    offset += 4;
    if (count > 100000)
        return;
    for (quint32 index = 0; index < count; ++index) {
        if (offset + 4 > block.size())
            return;
        const quint32 length = le32(block, offset);
        offset += 4;
        if (length > quint32(block.size() - offset))
            return;
        const QByteArray item = block.mid(offset, int(length));
        offset += int(length);
        const int split = item.indexOf('=');
        if (split <= 0)
            continue;
        const QString key = QString::fromLatin1(item.left(split)).trimmed().toUpper();
        const QString value = firstValue(item.mid(split + 1));
        fields.parsed = true;
        if (key == QStringLiteral("TITLE"))
            fields.setTitle(value);
        else if (key == QStringLiteral("ARTIST"))
            fields.setArtist(value);
        else if (key == QStringLiteral("ALBUM"))
            fields.setAlbum(value);
        else if (key == QStringLiteral("GENRE"))
            fields.setGenre(value);
        else if (key == QStringLiteral("DATE") || key == QStringLiteral("YEAR"))
            fields.setYear(value);
    }
}

void parseApe(QFile& file, Fields& fields) {
    const qint64 size = file.size();
    auto footerAt = [&](qint64 position) -> QByteArray {
        if (position < 0 || position + 32 > size)
            return {};
        const QByteArray footer = readAt(file, position, 32);
        return footer.startsWith("APETAGEX") ? footer : QByteArray();
    };
    qint64 footerPos = size - 32;
    QByteArray footer = footerAt(footerPos);
    if (footer.isEmpty() && size >= 128 + 32) {
        const QByteArray id3 = readAt(file, size - 128, 3);
        if (id3 == "TAG") {
            footerPos = size - 128 - 32;
            footer = footerAt(footerPos);
        }
    }
    if (footer.size() != 32)
        return;
    const quint32 tagSize = le32(footer, 12);
    const quint32 count = le32(footer, 16);
    if (tagSize < 32 || tagSize > quint32(kMaxTagBytes) || count > 100000)
        return;
    const qint64 bodyPos = footerPos - (qint64(tagSize) - 32);
    if (bodyPos < 0)
        return;
    const QByteArray body = readAt(file, bodyPos, int(tagSize) - 32);
    if (body.size() != int(tagSize) - 32)
        return;
    int offset = body.startsWith("APETAGEX") ? 32 : 0;
    for (quint32 index = 0; index < count && offset + 8 <= body.size(); ++index) {
        const quint32 length = le32(body, offset);
        offset += 8;
        const int keyEnd = body.indexOf('\0', offset);
        if (keyEnd < 0 || length > quint32(body.size() - (keyEnd + 1)))
            return;
        const QString key = QString::fromLatin1(body.mid(offset, keyEnd - offset)).toUpper();
        const QByteArray value = body.mid(keyEnd + 1, int(length));
        offset = keyEnd + 1 + int(length);
        const QString text = firstValue(value);
        fields.parsed = true;
        if (key == QStringLiteral("TITLE"))
            fields.setTitle(text);
        else if (key == QStringLiteral("ARTIST"))
            fields.setArtist(text);
        else if (key == QStringLiteral("ALBUM"))
            fields.setAlbum(text);
        else if (key == QStringLiteral("GENRE"))
            fields.setGenre(text);
        else if (key == QStringLiteral("YEAR") || key == QStringLiteral("DATE"))
            fields.setYear(text);
    }
}

int mpegBitrate(int versionBits, int layerBits, int index) {
    static const int v1l1[] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
    static const int v1l2[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
    static const int v1l3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int v2l1[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
    static const int v2l3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    if (index <= 0 || index >= 15)
        return 0;
    const bool mpeg1 = versionBits == 3;
    if (layerBits == 3)
        return (mpeg1 ? v1l1 : v2l1)[index];
    if (layerBits == 2)
        return (mpeg1 ? v1l2 : v2l3)[index];
    if (layerBits == 1)
        return (mpeg1 ? v1l3 : v2l3)[index];
    return 0;
}

int mpegSampleRate(int versionBits, int index) {
    static const int rates[][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
    if (versionBits > 3 || index > 2)
        return 0;
    return rates[versionBits][index];
}

struct MpegFrame {
    int versionBits = 0;
    int layerBits = 0;
    int bitrate = 0;
    int sampleRate = 0;
    int padding = 0;
    int channelMode = 0;
    int samples = 0;
    int frameBytes = 0;
};

std::optional<MpegFrame> readMpegFrame(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 4 > data.size())
        return std::nullopt;
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    if (bytes[0] != 0xff || (bytes[1] & 0xe0) != 0xe0)
        return std::nullopt;
    MpegFrame frame;
    frame.versionBits = (bytes[1] >> 3) & 0x03;
    frame.layerBits = (bytes[1] >> 1) & 0x03;
    const int bitrateIndex = (bytes[2] >> 4) & 0x0f;
    const int rateIndex = (bytes[2] >> 2) & 0x03;
    frame.padding = (bytes[2] >> 1) & 0x01;
    frame.channelMode = (bytes[3] >> 6) & 0x03;
    if (frame.versionBits == 1 || frame.layerBits == 0 || rateIndex == 3)
        return std::nullopt;
    frame.bitrate = mpegBitrate(frame.versionBits, frame.layerBits, bitrateIndex) * 1000;
    frame.sampleRate = mpegSampleRate(frame.versionBits, rateIndex);
    if (frame.bitrate <= 0 || frame.sampleRate <= 0)
        return std::nullopt;
    const bool mpeg1 = frame.versionBits == 3;
    if (frame.layerBits == 3) {
        frame.samples = 384;
        frame.frameBytes = ((12 * frame.bitrate / frame.sampleRate) + frame.padding) * 4;
    } else if (frame.layerBits == 1 && !mpeg1) {
        frame.samples = 576;
        frame.frameBytes = (72 * frame.bitrate / frame.sampleRate) + frame.padding;
    } else {
        frame.samples = 1152;
        frame.frameBytes = (144 * frame.bitrate / frame.sampleRate) + frame.padding;
    }
    if (frame.frameBytes < 24)
        return std::nullopt;
    return frame;
}

void parseMpegDuration(QFile& file, qint64 audioOffset, Fields& fields) {
    if (audioOffset < 0 || audioOffset >= file.size())
        return;
    const int probe = int(std::min<qint64>(file.size() - audioOffset, 256 * 1024));
    const QByteArray data = readAt(file, audioOffset, probe);
    for (int offset = 0; offset + 4 < data.size(); ++offset) {
        if (uchar(data.at(offset)) != 0xff)
            continue;
        const std::optional<MpegFrame> frame = readMpegFrame(data, offset);
        if (!frame.has_value())
            continue;
        if (offset + frame->frameBytes + 1 < data.size()) {
            const std::optional<MpegFrame> next = readMpegFrame(data, offset + frame->frameBytes);
            if (!next.has_value())
                continue;
        }
        const bool mpeg1 = frame->versionBits == 3;
        const bool mono = frame->channelMode == 3;
        const int side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        const int tagAt = offset + 4 + side;
        qint64 frames = 0;
        if (tagAt + 16 <= data.size()) {
            const QByteArray marker = data.mid(tagAt, 4);
            if (marker == "Xing" || marker == "Info") {
                const quint32 flags = be32(data, tagAt + 4);
                if ((flags & 1) && tagAt + 12 <= data.size())
                    frames = be32(data, tagAt + 8);
            }
        }
        if (frames > 0) {
            fields.setDuration(double(frames) * frame->samples / double(frame->sampleRate));
        } else {
            qint64 audioBytes = file.size() - (audioOffset + offset);
            if (file.size() >= 128) {
                const QByteArray tail = readAt(file, file.size() - 128, 3);
                if (tail == "TAG")
                    audioBytes -= 128;
            }
            if (audioBytes > 0)
                fields.setDuration(double(audioBytes) * 8.0 / double(frame->bitrate));
        }
        fields.parsed = true;
        return;
    }
}

void parseFlac(QFile& file, Fields& fields) {
    const QByteArray magic = readAt(file, 0, 4);
    if (magic != "fLaC")
        return;
    qint64 position = 4;
    bool last = false;
    for (int guard = 0; !last && guard < 64 && position + 4 <= file.size(); ++guard) {
        const QByteArray header = readAt(file, position, 4);
        if (header.size() != 4)
            return;
        const int blockType = uchar(header.at(0)) & 0x7f;
        const int blockSize = (uchar(header.at(1)) << 16) | (uchar(header.at(2)) << 8) | uchar(header.at(3));
        last = uchar(header.at(0)) & 0x80;
        position += 4;
        if (blockSize < 0 || position + blockSize > file.size())
            return;
        if (blockType == 0 && blockSize >= 34) {
            const QByteArray info = readAt(file, position, 34);
            if (info.size() == 34) {
                const quint64 word = be64(info, 10);
                const quint32 sampleRate = quint32(word >> 44);
                const quint64 samples = word & ((quint64(1) << 36) - 1);
                if (sampleRate > 0)
                    fields.setDuration(double(samples) / double(sampleRate));
            }
        } else if (blockType == 4 && blockSize <= kMaxTagBytes) {
            const QByteArray comment = readAt(file, position, blockSize);
            applyVorbis(fields, comment);
        }
        position += blockSize;
    }
    fields.parsed = true;
}

QList<QByteArray> oggPackets(QFile& file, int limit) {
    QList<QByteArray> packets;
    QByteArray current;
    qint64 position = 0;
    const qint64 end = std::min(file.size(), qint64(limit));
    while (position + 27 <= end && packets.size() < 4) {
        const QByteArray header = readAt(file, position, 27);
        if (header.size() != 27 || !header.startsWith("OggS") || header.at(4) != '\0')
            break;
        const int segments = uchar(header.at(26));
        const QByteArray table = readAt(file, position + 27, segments);
        if (table.size() != segments)
            break;
        int body = 0;
        for (unsigned char length : table)
            body += length;
        const QByteArray data = readAt(file, position + 27 + segments, body);
        if (data.size() != body)
            break;
        int cursor = 0;
        for (int index = 0; index < table.size(); ++index) {
            const int length = uchar(table.at(index));
            current.append(data.mid(cursor, length));
            cursor += length;
            if (length < 255) {
                packets.append(current);
                current.clear();
                if (packets.size() >= 4)
                    break;
            }
        }
        position += 27 + segments + body;
    }
    return packets;
}

qint64 lastOggGranule(QFile& file) {
    const int window = int(std::min<qint64>(file.size(), 65536));
    if (window < 14)
        return -1;
    const QByteArray tail = readAt(file, file.size() - window, window);
    qint64 granule = -1;
    int search = 0;
    while (true) {
        const int found = tail.indexOf("OggS", search);
        if (found < 0 || found + 14 > tail.size())
            break;
        if (tail.at(found + 4) == '\0') {
            const qint64 value = qint64(le64(tail, found + 6));
            if (value >= 0)
                granule = value;
        }
        search = found + 4;
    }
    return granule;
}

void parseOgg(QFile& file, Fields& fields) {
    const QList<QByteArray> packets = oggPackets(file, 256 * 1024);
    if (packets.isEmpty())
        return;
    const qint64 granule = lastOggGranule(file);
    const QByteArray& first = packets.first();
    if (first.startsWith("\x01vorbis") && first.size() >= 16) {
        const quint32 sampleRate = le32(first, 12);
        if (packets.size() >= 2 && packets.at(1).startsWith("\x03vorbis"))
            applyVorbis(fields, packets.at(1).mid(7));
        if (sampleRate > 0 && granule >= 0)
            fields.setDuration(double(granule) / double(sampleRate));
        fields.parsed = true;
        return;
    }
    if (first.startsWith("OpusHead") && first.size() >= 12) {
        const int preskip = le16(first, 10);
        if (packets.size() >= 2 && packets.at(1).startsWith("OpusTags"))
            applyVorbis(fields, packets.at(1).mid(8));
        if (granule >= preskip)
            fields.setDuration(double(granule - preskip) / 48000.0);
        fields.parsed = true;
    }
}

struct Atom {
    qint64 start = 0;
    qint64 size = 0;
    qint64 payload = 0;
    QByteArray type;
};

bool readAtom(QFile& file, qint64 position, qint64 end, Atom& atom) {
    if (position < 0 || position + 8 > end)
        return false;
    const QByteArray header = readAt(file, position, 8);
    if (header.size() != 8)
        return false;
    const quint32 size32 = be32(header, 0);
    atom.type = header.mid(4, 4);
    atom.start = position;
    int headerSize = 8;
    qint64 size = size32;
    if (size32 == 1) {
        const QByteArray extended = readAt(file, position + 8, 8);
        if (extended.size() != 8)
            return false;
        size = qint64(be64(extended, 0));
        headerSize = 16;
    } else if (size32 == 0) {
        size = end - position;
    }
    if (size < headerSize || position > end - size)
        return false;
    atom.size = size;
    atom.payload = position + headerSize;
    return true;
}

QString mp4Text(const QByteArray& data) {
    if (data.size() < 8)
        return {};
    const quint32 kind = be32(data, 0);
    QByteArray raw = data.mid(8);
    if (kind == 2)
        return decodeUtf16(QByteArray("\xfe\xff", 2) + raw);
    const QString text = QString::fromUtf8(raw).trimmed();
    return text.contains(QChar::ReplacementCharacter) ? QString::fromLatin1(raw).trimmed() : text;
}

void parseIlst(QFile& file, qint64 start, qint64 end, Fields& fields) {
    qint64 position = start;
    while (position + 8 <= end) {
        Atom item;
        if (!readAtom(file, position, end, item))
            break;
        qint64 child = item.payload;
        const qint64 itemEnd = item.start + item.size;
        while (child + 8 <= itemEnd) {
            Atom data;
            if (!readAtom(file, child, itemEnd, data))
                break;
            if (data.type == "data") {
                const int length = int(std::min<qint64>(data.start + data.size - data.payload, 4096));
                const QByteArray bytes = readAt(file, data.payload, length);
                const auto typeBytes = [](unsigned char a, char b, char c, char d) {
                    const char bytes[] = {char(a), b, c, d};
                    return QByteArray(bytes, 4);
                };
                const QByteArray titleKey = typeBytes(0xa9, 'n', 'a', 'm');
                const QByteArray artistKey = typeBytes(0xa9, 'A', 'R', 'T');
                const QByteArray albumKey = typeBytes(0xa9, 'a', 'l', 'b');
                const QByteArray genreKey = typeBytes(0xa9, 'g', 'e', 'n');
                const QByteArray yearKey = typeBytes(0xa9, 'd', 'a', 'y');
                if (item.type == "gnre" && bytes.size() >= 10) {
                    fields.setGenre(genreName(be16(bytes, 8) - 1));
                } else {
                    const QString text = mp4Text(bytes);
                    if (item.type == titleKey)
                        fields.setTitle(text);
                    else if (item.type == artistKey || item.type == "aART")
                        fields.setArtist(text);
                    else if (item.type == albumKey)
                        fields.setAlbum(text);
                    else if (item.type == genreKey)
                        fields.setGenre(text);
                    else if (item.type == yearKey)
                        fields.setYear(text);
                }
            }
            child = data.start + data.size;
        }
        position = item.start + item.size;
    }
}

void parseMvhd(const QByteArray& data, Fields& fields) {
    if (data.size() < 20)
        return;
    const int version = uchar(data.at(0));
    quint64 duration = 0;
    quint32 scale = 0;
    if (version == 0 && data.size() >= 20) {
        scale = be32(data, 12);
        duration = be32(data, 16);
    } else if (version == 1 && data.size() >= 32) {
        scale = be32(data, 20);
        duration = be64(data, 24);
    }
    if (scale > 0)
        fields.setDuration(double(duration) / double(scale));
}

void walkMp4(QFile& file, qint64 start, qint64 end, bool fullBox, int depth, Fields& fields) {
    if (depth > 8 || start >= end)
        return;
    qint64 position = start + (fullBox ? 4 : 0);
    while (position + 8 <= end) {
        Atom atom;
        if (!readAtom(file, position, end, atom))
            break;
        const qint64 atomEnd = atom.start + atom.size;
        if (atom.type == "mvhd") {
            const QByteArray data = readAt(file, atom.payload, int(std::min<qint64>(atomEnd - atom.payload, 48)));
            parseMvhd(data, fields);
        } else if (atom.type == "ilst") {
            parseIlst(file, atom.payload, atomEnd, fields);
        } else if (atom.type == "moov" || atom.type == "trak" || atom.type == "mdia" || atom.type == "minf"
                   || atom.type == "udta" || atom.type == "edts" || atom.type == "moof" || atom.type == "traf") {
            walkMp4(file, atom.payload, atomEnd, false, depth + 1, fields);
        } else if (atom.type == "meta") {
            walkMp4(file, atom.payload, atomEnd, true, depth + 1, fields);
        }
        position = atomEnd;
    }
}

void parseMp4(QFile& file, Fields& fields) {
    walkMp4(file, 0, file.size(), false, 0, fields);
    if (fields.duration.has_value() || fields.hasText())
        fields.parsed = true;
}

double extended80(const QByteArray& data, int offset) {
    const int exponent = be16(data, offset);
    const quint64 mantissa = be64(data, offset + 2);
    const int sign = (exponent >> 15) & 1;
    const int power = (exponent & 0x7fff) - 16383 - 63;
    if ((exponent & 0x7fff) == 0 && mantissa == 0)
        return 0;
    const double magnitude = std::ldexp(double(mantissa), power);
    return sign ? -magnitude : magnitude;
}

void parseWav(QFile& file, Fields& fields) {
    const QByteArray riff = readAt(file, 0, 12);
    if (riff.size() != 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WAVE")
        return;
    quint32 byteRate = 0;
    qint64 position = 12;
    while (position + 8 <= file.size()) {
        const QByteArray header = readAt(file, position, 8);
        if (header.size() != 8)
            break;
        const QByteArray id = header.left(4);
        const quint32 size = le32(header, 4);
        const qint64 dataPos = position + 8;
        if (size > quint32(std::numeric_limits<int>::max()) || dataPos + qint64(size) > file.size())
            break;
        if (id == "fmt " && size >= 12) {
            const QByteArray fmt = readAt(file, dataPos, int(std::min<quint32>(size, 32)));
            if (fmt.size() >= 12)
                byteRate = le32(fmt, 8);
        } else if (id == "data" && byteRate > 0) {
            fields.setDuration(double(size) / double(byteRate));
        } else if (id == "LIST" && size >= 4) {
            const QByteArray list = readAt(file, dataPos, int(size));
            if (list.startsWith("INFO")) {
                int offset = 4;
                while (offset + 8 <= list.size()) {
                    const QByteArray chunk = list.mid(offset, 4);
                    const quint32 chunkSize = le32(list, offset + 4);
                    offset += 8;
                    if (chunkSize > quint32(list.size() - offset))
                        break;
                    const QString text = firstValue(list.mid(offset, int(chunkSize)));
                    if (chunk == "INAM")
                        fields.setTitle(text);
                    else if (chunk == "IART")
                        fields.setArtist(text);
                    else if (chunk == "IPRD" || chunk == "IALB")
                        fields.setAlbum(text);
                    else if (chunk == "IGNR")
                        fields.setGenre(text);
                    else if (chunk == "ICRD")
                        fields.setYear(text);
                    offset += int(chunkSize) + int(chunkSize & 1);
                }
            }
        }
        position = dataPos + size + (size & 1);
    }
    fields.parsed = true;
}

void parseAvi(QFile& file, Fields& fields) {
    const QByteArray riff = readAt(file, 0, 12);
    if (riff.size() != 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "AVI ")
        return;
    qint64 position = 12;
    while (position + 8 <= file.size()) {
        const QByteArray header = readAt(file, position, 8);
        if (header.size() != 8)
            break;
        const QByteArray id = header.left(4);
        const quint32 size = le32(header, 4);
        const qint64 dataPos = position + 8;
        if (id == "avih" && size >= 20) {
            const QByteArray avih = readAt(file, dataPos, 20);
            if (avih.size() == 20) {
                const quint32 micro = le32(avih, 0);
                const quint32 frames = le32(avih, 16);
                if (micro > 0 && frames > 0)
                    fields.setDuration(double(micro) * double(frames) / 1000000.0);
            }
        }
        if (id == "LIST")
            position = dataPos + 4;
        else {
            if (dataPos + size < dataPos)
                break;
            position = dataPos + size + (size & 1);
        }
        if (id != "LIST" && size > 1024 * 1024 && id != "JUNK") {
            if (fields.duration.has_value())
                break;
        }
    }
    if (fields.duration.has_value())
        fields.parsed = true;
}

void parseAiff(QFile& file, Fields& fields) {
    const QByteArray form = readAt(file, 0, 12);
    if (form.size() != 12 || !form.startsWith("FORM") || (form.mid(8, 4) != "AIFF" && form.mid(8, 4) != "AIFC"))
        return;
    quint32 frames = 0;
    double rate = 0;
    qint64 position = 12;
    while (position + 8 <= file.size()) {
        const QByteArray header = readAt(file, position, 8);
        if (header.size() != 8)
            break;
        const QByteArray id = header.left(4);
        const quint32 size = be32(header, 4);
        const qint64 dataPos = position + 8;
        if (dataPos + qint64(size) > file.size())
            break;
        if (id == "COMM" && size >= 18) {
            const QByteArray comm = readAt(file, dataPos, 18);
            if (comm.size() == 18) {
                frames = be32(comm, 2);
                rate = extended80(comm, 8);
            }
        } else if (id == "NAME" || id == "AUTH") {
            const QByteArray raw = readAt(file, dataPos, int(std::min<quint32>(size, 4096)));
            const QString text = firstValue(raw);
            if (id == "NAME")
                fields.setTitle(text);
            else
                fields.setArtist(text);
        }
        position = dataPos + size + (size & 1);
    }
    if (rate > 0)
        fields.setDuration(double(frames) / rate);
    fields.parsed = true;
}

QString jsonTag(const QJsonObject& tags, std::initializer_list<QString> keys) {
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        for (const QString& key : keys) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0 && it.value().isString())
                return it.value().toString().trimmed();
        }
    }
    return {};
}

void mergeFfprobe(const QString& path, Fields& fields) {
    const QString program = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (program.isEmpty())
        return;
    QProcess process;
    process.start(program, {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"),
                            QStringLiteral("json"), QStringLiteral("-show_format"), path});
    if (!process.waitForStarted(2000) || !process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(500);
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!document.isObject())
        return;
    const QJsonObject format = document.object().value(QStringLiteral("format")).toObject();
    if (!fields.duration.has_value()) {
        const QJsonValue durationValue = format.value(QStringLiteral("duration"));
        bool ok = false;
        double duration = 0;
        if (durationValue.isString())
            duration = durationValue.toString().toDouble(&ok);
        else if (durationValue.isDouble()) {
            duration = durationValue.toDouble();
            ok = true;
        }
        if (ok)
            fields.setDuration(duration);
    }
    const QJsonObject tags = format.value(QStringLiteral("tags")).toObject();
    fields.setTitle(jsonTag(tags, {QStringLiteral("title")}));
    fields.setArtist(jsonTag(tags, {QStringLiteral("artist")}));
    fields.setAlbum(jsonTag(tags, {QStringLiteral("album")}));
    fields.setGenre(jsonTag(tags, {QStringLiteral("genre")}));
    const QString date = jsonTag(tags, {QStringLiteral("date"), QStringLiteral("year")});
    fields.setYear(date);
}

bool audioOnlySuffix(const QString& suffix) {
    static const QSet<QString> suffixes = {
        QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("wav"),  QStringLiteral("aif"), QStringLiteral("aiff"),
        QStringLiteral("m4a"),  QStringLiteral("aac"),  QStringLiteral("wv"),  QStringLiteral("wma"),
        QStringLiteral("ape"),  QStringLiteral("mp2"),  QStringLiteral("alac"),
    };
    return suffixes.contains(suffix);
}

qint64 id3AudioOffset(QFile& file) {
    const QByteArray header = readAt(file, 0, 10);
    if (header.size() != 10 || !header.startsWith("ID3"))
        return 0;
    const int tagSize = syncsafe(header, 6);
    if (tagSize < 0)
        return 0;
    qint64 offset = 10 + tagSize;
    if (uchar(header.at(5)) & 0x10)
        offset += 10;
    return offset;
}

}  // namespace

std::optional<int> yearFromText(const QString& text) {
    QString digits;
    for (const QChar character : text) {
        if (character.isDigit()) {
            digits.append(character);
            if (digits.size() == 4)
                return digits.toInt();
        } else if (!digits.isEmpty()) {
            break;
        }
    }
    return std::nullopt;
}

QString formatDuration(std::optional<double> seconds) {
    if (!seconds.has_value() || *seconds < 0)
        return {};
    const int total = int(*seconds + 0.5);
    int minutes = total / 60;
    const int secs = total % 60;
    const int hours = minutes / 60;
    minutes %= 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QChar('0'));
}

TrackInfo readTrackInfo(const QString& path) {
    TrackInfo info;
    const QFileInfo fileInfo(path);
    info.title = fileInfo.completeBaseName();
    if (!fileInfo.isFile())
        return info;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return info;

    Fields fields;
    const QByteArray magic = readAt(file, 0, 12);
    const QString suffix = fileInfo.suffix().toLower();
    if (magic.startsWith("ID3") || suffix == QStringLiteral("mp3")) {
        parseId3v2(file, fields);
        parseMpegDuration(file, id3AudioOffset(file), fields);
    }
    if (magic.startsWith("fLaC") || suffix == QStringLiteral("flac"))
        parseFlac(file, fields);
    if (magic.startsWith("OggS") || suffix == QStringLiteral("ogg") || suffix == QStringLiteral("oga")
        || suffix == QStringLiteral("opus")) {
        parseOgg(file, fields);
    }
    if (magic.startsWith("RIFF") && magic.mid(8, 4) == "WAVE")
        parseWav(file, fields);
    else if (magic.startsWith("RIFF") && magic.mid(8, 4) == "AVI ")
        parseAvi(file, fields);
    else if (suffix == QStringLiteral("wav"))
        parseWav(file, fields);
    else if (suffix == QStringLiteral("avi"))
        parseAvi(file, fields);
    if (magic.startsWith("FORM") || suffix == QStringLiteral("aiff") || suffix == QStringLiteral("aif"))
        parseAiff(file, fields);
    if (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("mp4") || suffix == QStringLiteral("m4v")
        || suffix == QStringLiteral("mov") || magic.mid(4, 4) == "ftyp") {
        parseMp4(file, fields);
    }
    parseApe(file, fields);
    parseId3v1(file, fields);

    if (!fields.duration.has_value() && fields.tlen.has_value())
        fields.duration = fields.tlen;
    const bool trustNative = fields.parsed && fields.duration.has_value()
        && (audioOnlySuffix(suffix) || fields.hasText());
    if (!trustNative)
        mergeFfprobe(path, fields);

    if (!fields.title.isEmpty())
        info.title = fields.title;
    info.artist = fields.artist;
    info.album = fields.album;
    info.genre = fields.genre;
    info.duration = fields.duration;
    info.year = yearFromText(fields.yearText);
    return info;
}
