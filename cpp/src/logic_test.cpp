#include "error.hpp"
#include "playlists.hpp"
#include "settings.hpp"
#include "shlex.hpp"
#include "tags.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

int gFailures = 0;

void expect(bool condition, const QString& message) {
    if (condition)
        return;
    std::cerr << message.toStdString() << '\n';
    ++gFailures;
}

void expectEqual(const QString& actual, const QString& expected, const QString& message) {
    if (actual == expected)
        return;
    std::cerr << message.toStdString() << "\n  actual: " << actual.toStdString()
              << "\n  expected: " << expected.toStdString() << '\n';
    ++gFailures;
}

void testShlex() {
    expectEqual(splitCommand(QStringLiteral("vlc")).join('|'), QStringLiteral("vlc"), QStringLiteral("vlc"));
    expectEqual(splitCommand(QStringLiteral("vlc --intf dummy")).join('|'), QStringLiteral("vlc|--intf|dummy"),
                QStringLiteral("flags"));
    expectEqual(splitCommand(QStringLiteral("vlc \"my file.mp3\"")).join('|'), QStringLiteral("vlc|my file.mp3"),
                QStringLiteral("double quotes"));
    expectEqual(splitCommand(QStringLiteral("vlc 'my file.mp3'")).join('|'), QStringLiteral("vlc|my file.mp3"),
                QStringLiteral("single quotes"));
    expectEqual(splitCommand(QStringLiteral("vlc a\\ b")).join('|'), QStringLiteral("vlc|a b"), QStringLiteral("escape space"));
    expectEqual(splitCommand(QStringLiteral("vlc \"a\\\"b\"")).join('|'), QStringLiteral("vlc|a\"b"),
                QStringLiteral("escaped quote"));
    const QStringList empty = splitCommand(QStringLiteral("vlc \"\""));
    expect(empty.size() == 2 && empty.at(1).isEmpty(), QStringLiteral("empty quotes"));
    expectEqual(splitCommand(QStringLiteral("vlc \"a\\b\"")).join('|'), QStringLiteral("vlc|a\\b"),
                QStringLiteral("kept backslash"));
    expectEqual(splitCommand(QStringLiteral("  vlc   x  ")).join('|'), QStringLiteral("vlc|x"), QStringLiteral("whitespace"));
    expectEqual(splitCommand(QStringLiteral("vlc # not comment")).join('|'), QStringLiteral("vlc|#|not|comment"),
                QStringLiteral("hash"));
    bool threw = false;
    try {
        splitCommand(QStringLiteral("vlc \"unterminated"));
    } catch (const AppError& exc) {
        threw = exc.message() == QStringLiteral("No closing quotation");
    }
    expect(threw, QStringLiteral("unclosed quote"));
    threw = false;
    try {
        splitCommand(QStringLiteral("vlc \\"));
    } catch (const AppError& exc) {
        threw = exc.message() == QStringLiteral("No escaped character");
    }
    expect(threw, QStringLiteral("dangling escape"));
}

void testPlaylists(const QString& root) {
    const QString folder = root + QStringLiteral("/lists");
    QDir().mkpath(folder);
    const QString created = createPlaylist(folder, QStringLiteral("  Evening  "));
    expect(created.endsWith(QStringLiteral("/Evening.m3u8")), QStringLiteral("created name"));
    expect(QFileInfo(created).exists(), QStringLiteral("created file"));
    bool duplicate = false;
    try {
        createPlaylist(folder, QStringLiteral("Evening.m3u8"));
    } catch (const AppError&) {
        duplicate = true;
    }
    expect(duplicate, QStringLiteral("duplicate playlist"));

    const QString renamed = renamePlaylist(created, QStringLiteral("Night"));
    expect(renamed.endsWith(QStringLiteral("/Night.m3u8")), QStringLiteral("renamed"));
    saveTracks(renamed, {QStringLiteral("/music/one.mp3"), QStringLiteral("/music/two.flac")});
    const QStringList loaded = loadTracks(renamed);
    expect(loaded.size() == 2 && loaded.at(0) == QStringLiteral("/music/one.mp3"), QStringLiteral("saved tracks"));

    QFile relative(folder + QStringLiteral("/relative.m3u"));
    expect(relative.open(QIODevice::WriteOnly), QStringLiteral("open relative m3u"));
    relative.write("#EXTM3U\n#EXTINF:-1,song\nsong.mp3\nfile:///tmp/My%20Song.mp3\nhttp://example.com/a.mp3\n");
    relative.close();
    const QStringList relativeTracks = loadTracks(relative.fileName());
    expectEqual(relativeTracks.value(0), QDir(folder).filePath(QStringLiteral("song.mp3")), QStringLiteral("relative m3u"));
    expectEqual(relativeTracks.value(1), QStringLiteral("/tmp/My Song.mp3"), QStringLiteral("file url"));
    expect(relativeTracks.size() == 2, QStringLiteral("remote url skipped"));

    QFile pls(folder + QStringLiteral("/radio.pls"));
    expect(pls.open(QIODevice::WriteOnly), QStringLiteral("open pls"));
    pls.write("[playlist]\nFile2=second.mp3\nFile1=\"first.mp3\"\n");
    pls.close();
    const QStringList plsTracks = loadTracks(pls.fileName());
    expectEqual(plsTracks.value(0), QDir::cleanPath(folder + QStringLiteral("/first.mp3")), QStringLiteral("pls order"));
    expect(plsTracks.value(1).endsWith(QStringLiteral("/second.mp3")), QStringLiteral("pls second"));

    QFile xspf(folder + QStringLiteral("/list.xspf"));
    expect(xspf.open(QIODevice::WriteOnly), QStringLiteral("open xspf"));
    xspf.write("<?xml version=\"1.0\"?>\n"
               "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
               "<trackList><track><location>/tmp/plain.mp3</location></track></trackList></playlist>\n");
    xspf.close();
    const QString imported = importPlaylist(xspf.fileName(), folder);
    expect(imported.endsWith(QStringLiteral("/list.m3u8")), QStringLiteral("import name"));
    expectEqual(loadTracks(imported).value(0), QStringLiteral("/tmp/plain.mp3"), QStringLiteral("import tracks"));
    const QString again = importPlaylist(xspf.fileName(), folder);
    expect(again.endsWith(QStringLiteral("/list (2).m3u8")), QStringLiteral("unique import"));

    const QStringList names = listPlaylists(folder);
    expect(names.size() == 3, QStringLiteral("playlist count"));
    expect(names.at(0).endsWith(QStringLiteral("/list.m3u8")), QStringLiteral("sort first"));

    bool badName = false;
    try {
        filenameForName(QStringLiteral("a/b"));
    } catch (const AppError&) {
        badName = true;
    }
    expect(badName, QStringLiteral("slash rejected"));
    deletePlaylist(renamed);
    expect(!QFileInfo(renamed).exists(), QStringLiteral("deleted"));
}

void testSettings(const QString& root) {
    const QString path = root + QStringLiteral("/settings.json");
    qputenv("PLAYLIST_MANAGER_CONFIG", path.toUtf8());
    Settings settings;
    settings.playlistDir = QStringLiteral("/playlists");
    settings.vlcCommand = QStringLiteral("  vlc --intf dummy  ");
    settings.columnWidths = {56, 220, 150, 150, 72, 120, 72, 180, 260};
    saveSettings(settings);
    const Settings loaded = loadSettings();
    expectEqual(loaded.playlistDir, QStringLiteral("/playlists"), QStringLiteral("playlist dir"));
    expectEqual(loaded.vlcCommand, QStringLiteral("vlc --intf dummy"), QStringLiteral("vlc command"));
    expect(loaded.columnWidths.size() == 9 && loaded.columnWidths.at(1) == 220, QStringLiteral("widths"));

    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), QStringLiteral("rewrite settings"));
    file.write("{}\n");
    file.close();
    const Settings defaults = loadSettings();
    expect(defaults.vlcCommand == QStringLiteral("vlc"), QStringLiteral("default vlc"));
    expect(defaults.columnWidths.at(0) == 56, QStringLiteral("default width"));
}

QByteArray textFrame(const char* id, const QByteArray& text) {
    QByteArray body;
    body.append(char(0));
    body.append(text);
    QByteArray frame(id, 4);
    const quint32 size = quint32(body.size());
    frame.append(char((size >> 24) & 0xff));
    frame.append(char((size >> 16) & 0xff));
    frame.append(char((size >> 8) & 0xff));
    frame.append(char(size & 0xff));
    frame.append(char(0));
    frame.append(char(0));
    frame.append(body);
    return frame;
}

void testTags(const QString& root) {
    expect(yearFromText(QStringLiteral("1999-01-01")) == 1999, QStringLiteral("year"));
    expect(!yearFromText(QStringLiteral("May")).has_value(), QStringLiteral("no year"));
    expectEqual(formatDuration(std::optional<double>(65)), QStringLiteral("1:05"), QStringLiteral("duration minutes"));
    expectEqual(formatDuration(std::optional<double>(3661)), QStringLiteral("1:01:01"), QStringLiteral("duration hours"));
    expect(formatDuration(std::nullopt).isEmpty(), QStringLiteral("empty duration"));

    QByteArray frames;
    frames += textFrame("TIT2", "Hello");
    frames += textFrame("TPE1", "Ada");
    frames += textFrame("TALB", "Demo");
    frames += textFrame("TYER", "1999");
    frames += textFrame("TCON", "(17)");
    QByteArray tag = QByteArray("ID3") + char(3) + char(0) + char(0);
    const quint32 size = quint32(frames.size());
    tag.append(char((size >> 21) & 0x7f));
    tag.append(char((size >> 14) & 0x7f));
    tag.append(char((size >> 7) & 0x7f));
    tag.append(char(size & 0x7f));
    tag += frames;
    const QString id3Path = root + QStringLiteral("/tagged.mp3");
    QFile id3(id3Path);
    expect(id3.open(QIODevice::WriteOnly), QStringLiteral("write id3"));
    id3.write(tag);
    id3.close();
    const TrackInfo id3Info = readTrackInfo(id3Path);
    expectEqual(id3Info.title, QStringLiteral("Hello"), QStringLiteral("id3 title"));
    expectEqual(id3Info.artist, QStringLiteral("Ada"), QStringLiteral("id3 artist"));
    expectEqual(id3Info.album, QStringLiteral("Demo"), QStringLiteral("id3 album"));
    expect(id3Info.year == 1999, QStringLiteral("id3 year"));
    expectEqual(id3Info.genre, QStringLiteral("Rock"), QStringLiteral("id3 genre"));

    const auto expectTags = [](const QString& label, const TrackInfo& info, double duration) {
        expectEqual(info.title, QStringLiteral("Hello"), label + QStringLiteral(" title"));
        expectEqual(info.artist, QStringLiteral("Ada"), label + QStringLiteral(" artist"));
        expectEqual(info.album, QStringLiteral("Demo"), label + QStringLiteral(" album"));
        expect(info.year == 1999, label + QStringLiteral(" year"));
        expectEqual(info.genre, QStringLiteral("Jazz"), label + QStringLiteral(" genre"));
        expect(info.duration.has_value() && std::abs(*info.duration - duration) < 0.05,
               QStringLiteral("%1 duration %2").arg(label).arg(info.duration.value_or(-1)));
    };

    QByteArray comment;
    auto appendLe32 = [&comment](quint32 value) {
        for (int shift = 0; shift < 32; shift += 8)
            comment.append(char((value >> shift) & 0xff));
    };
    auto appendItem = [&](const char* text) {
        appendLe32(quint32(std::strlen(text)));
        comment.append(text);
    };
    appendLe32(0);
    appendLe32(5);
    appendItem("TITLE=Hello");
    appendItem("ARTIST=Ada");
    appendItem("ALBUM=Demo");
    appendItem("DATE=1999");
    appendItem("GENRE=Jazz");

    QByteArray flac("fLaC", 4);
    flac.append(char(0));
    flac.append(char(0));
    flac.append(char(0));
    flac.append(char(34));
    flac.append(QByteArray(10, '\0'));
    const quint64 streamInfo = (quint64(44100) << 44) | 44100;
    for (int shift = 56; shift >= 0; shift -= 8)
        flac.append(char((streamInfo >> shift) & 0xff));
    flac.append(QByteArray(16, '\0'));
    flac.append(char(0x84));
    flac.append(char((comment.size() >> 16) & 0xff));
    flac.append(char((comment.size() >> 8) & 0xff));
    flac.append(char(comment.size() & 0xff));
    flac.append(comment);
    const QString flacPath = root + QStringLiteral("/sample.flac");
    QFile flacFile(flacPath);
    expect(flacFile.open(QIODevice::WriteOnly), QStringLiteral("write flac"));
    flacFile.write(flac);
    flacFile.close();
    expectTags(QStringLiteral("flac"), readTrackInfo(flacPath), 1.0);

    auto oggPage = [](const QByteArray& packet, unsigned char headerType, quint32 sequence, qint64 granule) {
        QByteArray page("OggS", 4);
        page.append(char(0));
        page.append(char(headerType));
        for (int shift = 0; shift < 64; shift += 8)
            page.append(char((granule >> shift) & 0xff));
        for (int index = 0; index < 4; ++index)
            page.append(char(1));
        for (int shift = 0; shift < 32; shift += 8)
            page.append(char((sequence >> shift) & 0xff));
        page.append(QByteArray(4, '\0'));
        page.append(char(1));
        page.append(char(packet.size()));
        page.append(packet);
        return page;
    };
    QByteArray vorbisId(30, '\0');
    vorbisId[0] = 1;
    vorbisId.replace(1, 6, "vorbis");
    vorbisId[11] = 2;
    vorbisId[12] = char(44100 & 0xff);
    vorbisId[13] = char((44100 >> 8) & 0xff);
    vorbisId[14] = char((44100 >> 16) & 0xff);
    vorbisId[29] = 1;
    QByteArray vorbisComment;
    vorbisComment.append(char(3));
    vorbisComment.append("vorbis");
    vorbisComment.append(comment);
    vorbisComment.append(char(1));
    const QString oggPath = root + QStringLiteral("/sample.ogg");
    QFile oggFile(oggPath);
    expect(oggFile.open(QIODevice::WriteOnly), QStringLiteral("write ogg"));
    oggFile.write(oggPage(vorbisId, 2, 0, 0));
    oggFile.write(oggPage(vorbisComment, 0, 1, 88200));
    oggFile.close();
    expectTags(QStringLiteral("ogg"), readTrackInfo(oggPath), 2.0);

    QByteArray opusHead("OpusHead", 8);
    opusHead.append(char(1));
    opusHead.append(char(1));
    opusHead.append(QByteArray(2, '\0'));
    opusHead.append(char(48000 & 0xff));
    opusHead.append(char((48000 >> 8) & 0xff));
    opusHead.append(QByteArray(2, '\0'));
    opusHead.append(QByteArray(2, '\0'));
    opusHead.append(char(0));
    QByteArray opusTags("OpusTags", 8);
    opusTags.append(comment);
    const QString opusPath = root + QStringLiteral("/sample.opus");
    QFile opusFile(opusPath);
    expect(opusFile.open(QIODevice::WriteOnly), QStringLiteral("write opus"));
    opusFile.write(oggPage(opusHead, 2, 0, 0));
    opusFile.write(oggPage(opusTags, 0, 1, 48000));
    opusFile.close();
    expectTags(QStringLiteral("opus"), readTrackInfo(opusPath), 1.0);

    auto appendBe32 = [](QByteArray& target, quint32 value) {
        target.append(char((value >> 24) & 0xff));
        target.append(char((value >> 16) & 0xff));
        target.append(char((value >> 8) & 0xff));
        target.append(char(value & 0xff));
    };
    auto atom = [&appendBe32](const QByteArray& type, const QByteArray& payload) {
        QByteArray result;
        appendBe32(result, quint32(8 + payload.size()));
        result.append(type);
        result.append(payload);
        return result;
    };
    const char titleType[] = {char(0xa9), 'n', 'a', 'm'};
    const char artistType[] = {char(0xa9), 'A', 'R', 'T'};
    const char albumType[] = {char(0xa9), 'a', 'l', 'b'};
    const char genreType[] = {char(0xa9), 'g', 'e', 'n'};
    const char yearType[] = {char(0xa9), 'd', 'a', 'y'};
    auto textAtom = [&](const char* type, const char* text) {
        QByteArray data(8, '\0');
        data[3] = 1;
        data.append(text);
        return atom(QByteArray(type, 4), atom(QByteArray("data", 4), data));
    };
    QByteArray ilst;
    ilst += textAtom(titleType, "Hello");
    ilst += textAtom(artistType, "Ada");
    ilst += textAtom(albumType, "Demo");
    ilst += textAtom(genreType, "Jazz");
    ilst += textAtom(yearType, "1999");
    QByteArray mvhd(100, '\0');
    mvhd[12] = 0;
    mvhd[13] = 0;
    mvhd[14] = 0x03;
    mvhd[15] = char(0xe8);
    mvhd[16] = 0;
    mvhd[17] = 0;
    mvhd[18] = 0x03;
    mvhd[19] = char(0xe8);
    QByteArray mp4;
    mp4 += atom(QByteArray("ftyp", 4), QByteArray("M4A ", 4) + QByteArray(4, '\0') + QByteArray("M4A ", 4));
    mp4 += atom(QByteArray("moov", 4),
                atom(QByteArray("mvhd", 4), mvhd)
                    + atom(QByteArray("udta", 4),
                           atom(QByteArray("meta", 4), QByteArray(4, '\0') + atom(QByteArray("ilst", 4), ilst))));
    const QString m4aPath = root + QStringLiteral("/sample.m4a");
    QFile m4aFile(m4aPath);
    expect(m4aFile.open(QIODevice::WriteOnly), QStringLiteral("write m4a"));
    m4aFile.write(mp4);
    m4aFile.close();
    expectTags(QStringLiteral("m4a"), readTrackInfo(m4aPath), 1.0);

    auto le32 = [](QByteArray& target, quint32 value) {
        for (int shift = 0; shift < 32; shift += 8)
            target.append(char((value >> shift) & 0xff));
    };
    auto infoChunk = [&](const char* id, const char* text) {
        QByteArray chunk(id, 4);
        const quint32 size = quint32(std::strlen(text) + 1);
        le32(chunk, size);
        chunk.append(text);
        chunk.append(char(0));
        if (size & 1)
            chunk.append(char(0));
        return chunk;
    };
    QByteArray info = QByteArray("INFO", 4) + infoChunk("INAM", "Hello") + infoChunk("IART", "Ada")
        + infoChunk("IPRD", "Demo") + infoChunk("IGNR", "Jazz") + infoChunk("ICRD", "1999");
    QByteArray fmt(16, '\0');
    fmt[0] = 1;
    fmt[2] = 1;
    fmt[4] = char(44100 & 0xff);
    fmt[5] = char((44100 >> 8) & 0xff);
    const quint32 byteRate = 88200;
    fmt[8] = char(byteRate & 0xff);
    fmt[9] = char((byteRate >> 8) & 0xff);
    fmt[10] = char((byteRate >> 16) & 0xff);
    fmt[12] = 2;
    fmt[14] = 16;
    const QByteArray samples(8820, '\0');
    QByteArray wave;
    auto chunk = [&](const char* id, const QByteArray& payload) {
        QByteArray result(id, 4);
        le32(result, quint32(payload.size()));
        result.append(payload);
        if (payload.size() & 1)
            result.append(char(0));
        return result;
    };
    const QByteArray body = chunk("fmt ", fmt) + chunk("data", samples) + chunk("LIST", info);
    wave.append("RIFF", 4);
    le32(wave, quint32(4 + body.size()));
    wave.append("WAVE", 4);
    wave.append(body);
    const QString wavPath = root + QStringLiteral("/sample.wav");
    QFile wavFile(wavPath);
    expect(wavFile.open(QIODevice::WriteOnly), QStringLiteral("write wav"));
    wavFile.write(wave);
    wavFile.close();
    expectTags(QStringLiteral("wav"), readTrackInfo(wavPath), 8820.0 / 88200.0);
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "temporary directory failed\n";
        return 1;
    }
    testShlex();
    testPlaylists(directory.path());
    testSettings(directory.path());
    testTags(directory.path());
    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
