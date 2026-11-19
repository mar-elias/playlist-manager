#include "mainwindow.hpp"

#include "error.hpp"
#include "playlists.hpp"
#include "shlex.hpp"

#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <sys/stat.h>

#include <algorithm>
#include <utility>

namespace {

constexpr char kAppName[] = "Mar-Elias Playlist Manager";
constexpr char kAppVersion[] = "0.1.0";
constexpr char kAppUrl[] = "https://github.com/mar-elias/playlist-manager";

QIcon videoIcon(const QColor& color) {
    static QHash<QRgb, QIcon> cache;
    const auto found = cache.constFind(color.rgba());
    if (found != cache.constEnd())
        return found.value();

    QIcon icon;
    const auto tint = [&icon, &color](const QString& resource) {
        QImage image(resource);
        if (image.isNull())
            return;
        image = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = qAlpha(line[x]);
                line[x] = qRgba(color.red(), color.green(), color.blue(), alpha);
            }
        }
        icon.addPixmap(QPixmap::fromImage(image));
    };
    tint(QStringLiteral(":/icons/video-16.png"));
    tint(QStringLiteral(":/icons/video-32.png"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("video-display-symbolic"));
    cache.insert(color.rgba(), icon);
    return icon;
}

QColor errorColor(const QWidget* widget) {
    const QColor text = widget->palette().color(widget->foregroundRole());
    if (text.lightness() > 128)
        return QColor(QStringLiteral("#ff8a80"));
    return QColor(QStringLiteral("#b00020"));
}

qint64 modificationStamp(const QString& path) {
    struct stat status;
    if (stat(path.toUtf8().constData(), &status) != 0)
        return -1;
    return qint64(status.st_mtim.tv_sec) * 1000000000LL + qint64(status.st_mtim.tv_nsec);
}

QStringList mediaPathsFromUrls(const QList<QUrl>& urls) {
    QStringList paths;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile())
            continue;
        QString raw = url.toLocalFile();
        if (raw.isEmpty())
            continue;
        if (raw == QStringLiteral("~") || raw.startsWith(QStringLiteral("~/")))
            raw = QDir::homePath() + raw.mid(1);
        const QFileInfo info(raw);
        const QString canonical = info.exists() ? info.canonicalFilePath() : QString();
        const QString resolved = QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
        if (isMediaFile(resolved))
            paths.append(resolved);
    }
    return paths;
}

QString locationText(const QString& path) {
    const QString parent = QFileInfo(path).path();
    if (parent.isEmpty() || parent == QStringLiteral("."))
        return {};
    return parent;
}

void acceptFileDrag(QDropEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

struct SortKey {
    bool missing = false;
    bool textual = false;
    double number = 0;
    QString text;
};

bool sortLess(const SortKey& left, const SortKey& right) {
    if (left.missing != right.missing)
        return !left.missing;
    if (left.textual || right.textual)
        return left.text < right.text;
    return left.number < right.number;
}

}  // namespace

PlaylistList::PlaylistList(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setMinimumWidth(260);
    setStyleSheet(QStringLiteral(
        "QListWidget { font-size: 12pt; } QListWidget::item { padding: 5px 6px; }"));
}

void PlaylistList::dragEnterEvent(QDragEnterEvent* event) {
    acceptFileDrag(event);
}

void PlaylistList::dragMoveEvent(QDragMoveEvent* event) {
    acceptFileDrag(event);
}

void PlaylistList::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const QListWidgetItem* item = itemAt(event->position().toPoint());
    const QString playlist = item == nullptr ? QString() : item->data(Qt::UserRole).toString();
    emit filesDropped(playlist, event->mimeData()->urls());
    event->acceptProposedAction();
}

void TitleDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    if (!index.data(kRoleVideo).toBool()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const QWidget* widget = option.widget;
    QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
    QStyleOptionViewItem background = option;
    initStyleOption(&background, index);
    background.icon = QIcon();
    background.features.setFlag(QStyleOptionViewItem::HasDecoration, false);
    background.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

    QStyleOptionViewItem text = background;
    text.text = index.data(Qt::DisplayRole).toString();
    text.state.setFlag(QStyle::State_HasFocus, false);
    const QRect full = option.rect;
    text.rect = QRect(full.x(), full.y(), std::max(0, full.width() - kIcon - kGap), full.height());
    style->drawControl(QStyle::CE_ItemViewItem, &text, painter, widget);

    const QColor color = background.palette.color(QPalette::Text);
    const QRect iconRect(full.right() - kIcon + 1, full.top(), kIcon, full.height());
    videoIcon(color).paint(painter, iconRect, Qt::AlignCenter);
}

TrackView::TrackView(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(9);
    setHeaderLabels({QStringLiteral("#"), QStringLiteral("Title"), QStringLiteral("Artist"),
                     QStringLiteral("Album"), QStringLiteral("Year"), QStringLiteral("Genre"),
                     QStringLiteral("Time"), QStringLiteral("File name"), QStringLiteral("Location")});
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setRootIsDecorated(false);
    setIndentation(0);
    setAlternatingRowColors(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setAcceptDrops(true);
    setDragEnabled(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setStyleSheet(QStringLiteral(
        "QTreeWidget { font-size: 12pt; } "
        "QTreeWidget::item { padding-top: 4px; padding-bottom: 4px; } "
        "QHeaderView { font-size: 10pt; }"));
    QHeaderView* header = this->header();
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setMinimumSectionSize(24);
    for (int column = 0; column < columnCount(); ++column)
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    setItemDelegateForColumn(1, new TitleDelegate(this));
}

void TrackView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
        emit removeRequested();
        return;
    }
    if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Up) {
        emit moveRequested(-1);
        return;
    }
    if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Down) {
        emit moveRequested(1);
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void TrackView::dragEnterEvent(QDragEnterEvent* event) {
    acceptFileDrag(event);
}

void TrackView::dragMoveEvent(QDragMoveEvent* event) {
    acceptFileDrag(event);
}

void TrackView::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    emit filesDropped(event->mimeData()->urls());
    event->acceptProposedAction();
}

QList<int> TrackView::playlistIndexes(const QList<QTreeWidgetItem*>& items) const {
    QList<int> indexes;
    for (QTreeWidgetItem* item : items) {
        bool ok = false;
        const int value = item->data(0, kRoleIndex).toInt(&ok);
        if (ok)
            indexes.append(value);
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
}

FileDropPage::FileDropPage(const QString& text, QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(this);
    label_ = new QLabel(text, this);
    label_->setAlignment(Qt::AlignCenter);
    label_->setWordWrap(true);
    layout->addWidget(label_);
}

void FileDropPage::setText(const QString& text) {
    label_->setText(text);
}

void FileDropPage::dragEnterEvent(QDragEnterEvent* event) {
    acceptFileDrag(event);
}

void FileDropPage::dragMoveEvent(QDragMoveEvent* event) {
    acceptFileDrag(event);
}

void FileDropPage::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    emit filesDropped(event->mimeData()->urls());
    event->acceptProposedAction();
}

SettingsDialog::SettingsDialog(PlaylistWindow* window) : QDialog(window), window_(window) {
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumWidth(480);
    auto* layout = new QFormLayout(this);
    folder_ = new QLineEdit(this);
    folder_->setPlaceholderText(QStringLiteral("Paste a folder path"));
    auto* choose = new QPushButton(QStringLiteral("Choose"), this);
    connect(choose, &QPushButton::clicked, this, &SettingsDialog::chooseFolder);
    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(choose);
    layout->addRow(QStringLiteral("Playlist folder"), folderRow);
    vlc_ = new QLineEdit(this);
    layout->addRow(QStringLiteral("VLC command"), vlc_);
    auto* hint = new QLabel(QStringLiteral("Selected tracks open with this command. Example: vlc"), this);
    hint->setWordWrap(true);
    layout->addRow(hint);
    auto* settingsPath = new QLineEdit(configFile(), this);
    settingsPath->setReadOnly(true);
    layout->addRow(QStringLiteral("Settings file"), settingsPath);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);
    refresh();
}

void SettingsDialog::refresh() {
    folder_->setText(window_->settings().playlistDir.trimmed());
    vlc_->setText(window_->settings().vlcCommand);
}

void SettingsDialog::chooseFolder() {
    QString current = folder_->text().trimmed();
    if (current.isEmpty() || !QFileInfo(current).isDir())
        current = window_->settings().playlistDir.trimmed();
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Playlist folder"), current);
    if (!path.isEmpty())
        folder_->setText(path);
}

void SettingsDialog::accept() {
    const QString folder = folder_->text().trimmed();
    if (!folder.isEmpty() && !QFileInfo(folder).isDir()) {
        QMessageBox::critical(this, QStringLiteral("Settings"),
                              QStringLiteral("That playlist folder is not available:\n%1").arg(folder));
        return;
    }
    window_->settings().playlistDir = folder;
    const QString command = vlc_->text().trimmed();
    window_->settings().vlcCommand = command.isEmpty() ? QStringLiteral("vlc") : command;
    try {
        saveSettings(window_->settings());
    } catch (const AppError& exc) {
        window_->showError(QStringLiteral("Could not save settings.\n%1").arg(exc.message()));
        return;
    }
    window_->reloadPlaylists();
    QDialog::accept();
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("About"));
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);
    auto* name = new QLabel(QString::fromLatin1(kAppName), this);
    name->setAlignment(Qt::AlignHCenter);
    QFont font = name->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    name->setFont(font);
    auto* version = new QLabel(QStringLiteral("Version %1").arg(QString::fromLatin1(kAppVersion)), this);
    version->setAlignment(Qt::AlignHCenter);
    auto* link = new QLabel(QStringLiteral("<a href=\"%1\">%1</a>").arg(QString::fromLatin1(kAppUrl)), this);
    link->setAlignment(Qt::AlignHCenter);
    link->setOpenExternalLinks(true);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(name);
    layout->addWidget(version);
    layout->addWidget(link);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

PlaylistWindow::PlaylistWindow(Settings settings, QWidget* parent)
    : QMainWindow(parent), settings_(std::move(settings)) {
    setWindowTitle(QStringLiteral("Playlist Manager"));
    resize(1180, 640);
    buildActions();
    buildToolbar();
    setStatusBar(new QStatusBar(this));

    playlistList_ = new PlaylistList(this);
    connect(playlistList_, &PlaylistList::filesDropped, this, &PlaylistWindow::onPlaylistFiles);
    connect(playlistList_, &QListWidget::itemSelectionChanged, this, &PlaylistWindow::onPlaylistSelected);
    playlistList_->setMaximumWidth(800);
    playlistList_->setMinimumWidth(100);

    trackView_ = new TrackView(this);
    connect(trackView_, &QTreeWidget::itemDoubleClicked, this, &PlaylistWindow::onTrackActivated);
    connect(trackView_, &QTreeWidget::itemSelectionChanged, this, &PlaylistWindow::updateActions);
    connect(trackView_, &TrackView::filesDropped, this, &PlaylistWindow::onTrackFiles);
    connect(trackView_, &TrackView::removeRequested, this, &PlaylistWindow::removeSelectedTracks);
    connect(trackView_, &TrackView::moveRequested, this, &PlaylistWindow::moveSelected);
    connect(trackView_->header(), &QHeaderView::sectionClicked, this, &PlaylistWindow::onColumnSorted);
    applyColumnWidths();
    connect(trackView_->header(), &QHeaderView::sectionResized, this, [this](int, int, int) { onColumnResized(); });
    columnSave_ = new QTimer(this);
    columnSave_->setSingleShot(true);
    columnSave_->setInterval(400);
    connect(columnSave_, &QTimer::timeout, this, &PlaylistWindow::writeColumnWidths);

    emptyPage_ = new FileDropPage(QStringLiteral("Drag audio and video files from Dolphin into this playlist."), this);
    nonePage_ = new FileDropPage(QStringLiteral("Select a playlist, or create a new one."), this);
    connect(emptyPage_, &FileDropPage::filesDropped, this, &PlaylistWindow::onTrackFiles);
    connect(nonePage_, &FileDropPage::filesDropped, this, &PlaylistWindow::onTrackFiles);

    trackStack_ = new QStackedWidget(this);
    trackStack_->addWidget(trackView_);
    trackStack_->addWidget(emptyPage_);
    trackStack_->addWidget(nonePage_);

    auto* trackBar = new QHBoxLayout();
    trackBar->addStretch(1);
    trackBar->addWidget(toolButton(moveUpAction_));
    trackBar->addWidget(toolButton(moveDownAction_));
    trackBar->addWidget(toolButton(removeAction_));
    auto* trackPane = new QWidget(this);
    trackPane->setMinimumWidth(500);
    trackPane->setMinimumHeight(300);
    auto* trackLayout = new QVBoxLayout(trackPane);
    trackLayout->setContentsMargins(0, 0, 0, 0);
    trackLayout->addLayout(trackBar);
    trackLayout->addWidget(trackStack_, 1);

    mainPane_ = new QSplitter(this);
    mainPane_->addWidget(playlistList_);
    mainPane_->addWidget(trackPane);
    mainPane_->setChildrenCollapsible(false);
    mainPane_->setStretchFactor(1, 1);
    mainPane_->setSizes({280, 900});

    setupPage_ = new FileDropPage(QStringLiteral("Each playlist is an M3U8 file in the folder you choose."), this);
    auto* choose = new QPushButton(QStringLiteral("Choose folder"), setupPage_);
    connect(choose, &QPushButton::clicked, this, &PlaylistWindow::chooseFolder);
    static_cast<QVBoxLayout*>(setupPage_->layout())->addWidget(choose, 0, Qt::AlignHCenter);

    mainStack_ = new QStackedWidget(this);
    mainStack_->addWidget(setupPage_);
    mainStack_->addWidget(mainPane_);
    setCentralWidget(mainStack_);
    reloadPlaylists();
}

void PlaylistWindow::buildActions() {
    QStyle* style = this->style();
    newAction_ = makeAction(QStringLiteral("New"), style->standardIcon(QStyle::SP_FileDialogNewFolder),
                            QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &PlaylistWindow::onNewPlaylist);
    importAction_ = makeAction(QStringLiteral("Import"), style->standardIcon(QStyle::SP_DialogOpenButton),
                               QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction_, &QAction::triggered, this, &PlaylistWindow::onImportPlaylist);
    exportAction_ = makeAction(QStringLiteral("Export"), style->standardIcon(QStyle::SP_DialogSaveButton),
                               QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction_, &QAction::triggered, this, &PlaylistWindow::onExportPlaylist);
    playAction_ = makeAction(QStringLiteral("Play"), style->standardIcon(QStyle::SP_MediaPlay),
                             QKeySequence(QStringLiteral("Ctrl+Return")));
    connect(playAction_, &QAction::triggered, this, &PlaylistWindow::play);
    renameAction_ = makeAction(QStringLiteral("Rename"), QIcon(), QKeySequence(QStringLiteral("F2")));
    connect(renameAction_, &QAction::triggered, this, &PlaylistWindow::onRenamePlaylist);
    deleteAction_ = makeAction(QStringLiteral("Delete playlist"), QIcon(), QKeySequence());
    connect(deleteAction_, &QAction::triggered, this, &PlaylistWindow::onDeletePlaylist);
    removeAction_ = makeAction(QStringLiteral("Remove"), style->standardIcon(QStyle::SP_TrashIcon), QKeySequence());
    connect(removeAction_, &QAction::triggered, this, &PlaylistWindow::removeSelectedTracks);
    moveUpAction_ = makeAction(QStringLiteral("Move up"), style->standardIcon(QStyle::SP_ArrowUp), QKeySequence());
    connect(moveUpAction_, &QAction::triggered, this, [this] { moveSelected(-1); });
    moveDownAction_ = makeAction(QStringLiteral("Move down"), style->standardIcon(QStyle::SP_ArrowDown), QKeySequence());
    connect(moveDownAction_, &QAction::triggered, this, [this] { moveSelected(1); });
    refreshAction_ = makeAction(QStringLiteral("Refresh"), style->standardIcon(QStyle::SP_BrowserReload),
                                QKeySequence::Refresh);
    connect(refreshAction_, &QAction::triggered, this, &PlaylistWindow::refresh);
    settingsAction_ = makeAction(QStringLiteral("Settings"), QIcon(), QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction_, &QAction::triggered, this, &PlaylistWindow::openSettings);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(newAction_);
    fileMenu->addAction(importAction_);
    fileMenu->addAction(exportAction_);
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(QStringLiteral("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* playlistMenu = menuBar()->addMenu(QStringLiteral("Playlist"));
    playlistMenu->addAction(playAction_);
    playlistMenu->addAction(renameAction_);
    playlistMenu->addAction(deleteAction_);
    playlistMenu->addAction(removeAction_);
    playlistMenu->addAction(moveUpAction_);
    playlistMenu->addAction(moveDownAction_);
    playlistMenu->addSeparator();
    playlistMenu->addAction(refreshAction_);

    QMenu* settingsMenu = menuBar()->addMenu(QStringLiteral("Settings"));
    settingsMenu->addAction(settingsAction_);
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    helpMenu->addAction(QStringLiteral("About"), this, &PlaylistWindow::openAbout);
}

QAction* PlaylistWindow::makeAction(const QString& text, const QIcon& icon, const QKeySequence& shortcut) {
    QAction* action = addAction(text);
    if (!icon.isNull())
        action->setIcon(icon);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    return action;
}

void PlaylistWindow::buildToolbar() {
    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(newAction_);
    toolbar->addAction(importAction_);
    toolbar->addAction(exportAction_);
    toolbar->addAction(playAction_);
    addToolBar(toolbar);
}

QWidget* PlaylistWindow::toolButton(QAction* action) {
    auto* button = new QToolButton(this);
    button->setDefaultAction(action);
    return button;
}

QString PlaylistWindow::playlistDirectory() const {
    const QString text = settings_.playlistDir.trimmed();
    if (text.isEmpty() || !QFileInfo(text).isDir())
        return {};
    return QDir::cleanPath(QFileInfo(text).absoluteFilePath());
}

void PlaylistWindow::reloadPlaylists() {
    reloadPlaylists(false, {});
}

void PlaylistWindow::reloadPlaylistsSelecting(const QString& path) {
    reloadPlaylists(true, path);
}

void PlaylistWindow::reloadPlaylists(bool useGiven, const QString& given) {
    rememberSelection();
    const QString select = useGiven ? given : currentPlaylist_;
    const QString directory = playlistDirectory();
    loading_ = true;
    playlistList_->clear();
    if (directory.isEmpty()) {
        showSetup();
        loading_ = false;
        currentPlaylist_.clear();
        tracks_.clear();
        showTracks();
        return;
    }

    mainStack_->setCurrentWidget(mainPane_);
    QStringList playlists;
    try {
        playlists = listPlaylists(directory);
    } catch (const AppError& exc) {
        loading_ = false;
        showError(QStringLiteral("Could not read the playlist folder.\n%1").arg(exc.message()));
        currentPlaylist_.clear();
        tracks_.clear();
        showTracks();
        return;
    }

    int chosen = -1;
    for (const QString& path : playlists) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), playlistList_);
        item->setData(Qt::UserRole, path);
        if (!select.isEmpty() && path == select)
            chosen = playlistList_->count() - 1;
    }
    loading_ = false;
    if (chosen < 0 && playlistList_->count() > 0)
        chosen = 0;
    if (chosen >= 0)
        playlistList_->setCurrentRow(chosen);
    else {
        currentPlaylist_.clear();
        tracks_.clear();
        showTracks();
    }
}

void PlaylistWindow::showSetup() {
    const QString text = settings_.playlistDir.trimmed();
    const QString message = text.isEmpty()
        ? QStringLiteral("Each playlist is an M3U8 file in the folder you choose.")
        : QStringLiteral("The playlist folder is not available:\n%1").arg(text);
    setupPage_->setText(message);
    mainStack_->setCurrentWidget(setupPage_);
}

void PlaylistWindow::refresh() {
    reloadPlaylists();
}

void PlaylistWindow::applyColumnWidths() {
    applyingColumns_ = true;
    QHeaderView* header = trackView_->header();
    for (int index = 0; index < trackView_->columnCount(); ++index) {
        header->setSectionResizeMode(index, QHeaderView::Interactive);
        if (index < settings_.columnWidths.size())
            trackView_->setColumnWidth(index, settings_.columnWidths.at(index));
    }
    applyingColumns_ = false;
}

void PlaylistWindow::onColumnResized() {
    if (applyingColumns_)
        return;
    settings_.columnWidths.clear();
    for (int index = 0; index < trackView_->columnCount(); ++index)
        settings_.columnWidths.append(trackView_->columnWidth(index));
    columnSave_->start();
}

void PlaylistWindow::writeColumnWidths() {
    try {
        saveSettings(settings_);
    } catch (const AppError& exc) {
        toast(QStringLiteral("Could not save column widths. %1").arg(exc.message()));
    }
}

TrackInfo PlaylistWindow::trackInfo(const QString& path) {
    const qint64 stamp = modificationStamp(path);
    const auto cached = tagCache_.constFind(path);
    if (cached != tagCache_.constEnd() && cached->first == stamp)
        return cached->second;
    const TrackInfo info = readTrackInfo(path);
    tagCache_.insert(path, {stamp, info});
    return info;
}

QList<int> PlaylistWindow::displayOrder() const {
    struct Entry {
        int index;
        SortKey key;
    };
    QList<Entry> entries;
    entries.reserve(tracks_.size());
    for (int index = 0; index < tracks_.size(); ++index) {
        const TrackInfo info = const_cast<PlaylistWindow*>(this)->trackInfo(tracks_.at(index));
        const QFileInfo file(tracks_.at(index));
        SortKey key;
        switch (sortColumn_) {
        case 0:
            key.number = index;
            break;
        case 1:
            key.textual = true;
            key.missing = info.title.isEmpty();
            key.text = info.title.toCaseFolded();
            break;
        case 2:
            key.textual = true;
            key.missing = info.artist.isEmpty();
            key.text = info.artist.toCaseFolded();
            break;
        case 3:
            key.textual = true;
            key.missing = info.album.isEmpty();
            key.text = info.album.toCaseFolded();
            break;
        case 4:
            key.missing = !info.year.has_value();
            key.number = info.year.value_or(0);
            break;
        case 5:
            key.textual = true;
            key.missing = info.genre.isEmpty();
            key.text = info.genre.toCaseFolded();
            break;
        case 6:
            key.missing = !info.duration.has_value();
            key.number = info.duration.value_or(0);
            break;
        case 7:
            key.textual = true;
            key.missing = file.fileName().isEmpty();
            key.text = file.fileName().toCaseFolded();
            break;
        default: {
            const QString location = locationText(tracks_.at(index));
            key.textual = true;
            key.missing = location.isEmpty();
            key.text = location.toCaseFolded();
            break;
        }
        }
        entries.append({index, key});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return sortLess(left.key, right.key);
    });
    if (sortDescending_) {
        const auto split = std::find_if(entries.begin(), entries.end(), [](const Entry& entry) {
            return entry.key.missing;
        });
        std::reverse(entries.begin(), split);
    }
    QList<int> order;
    order.reserve(entries.size());
    for (const Entry& entry : entries)
        order.append(entry.index);
    return order;
}

void PlaylistWindow::showTracks(const QList<int>& select) {
    trackView_->clear();
    const QColor missingColor = errorColor(trackView_);
    const QSet<int> chosen(select.begin(), select.end());
    QTreeWidgetItem* firstSelected = nullptr;
    for (int playlistIndex : displayOrder()) {
        const QString& track = tracks_.at(playlistIndex);
        const TrackInfo info = trackInfo(track);
        const QFileInfo file(track);
        const bool missing = !file.exists();
        auto* item = new QTreeWidgetItem(QStringList{
            QString::number(playlistIndex + 1),
            info.title,
            info.artist,
            info.album,
            info.yearText(),
            info.genre,
            formatDuration(info.duration),
            file.fileName(),
            locationText(track),
        });
        item->setData(0, kRoleIndex, playlistIndex);
        item->setData(0, kRolePath, track);
        if (isVideoFile(track))
            item->setData(1, kRoleVideo, true);
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(6, Qt::AlignRight | Qt::AlignVCenter);
        for (int column = 0; column < 9; ++column)
            item->setToolTip(column, track);
        if (missing) {
            for (int column = 0; column < 9; ++column)
                item->setForeground(column, missingColor);
        }
        trackView_->addTopLevelItem(item);
        if (chosen.contains(playlistIndex)) {
            item->setSelected(true);
            if (firstSelected == nullptr)
                firstSelected = item;
        }
    }
    if (firstSelected != nullptr)
        trackView_->scrollToItem(firstSelected);
    trackView_->header()->setSortIndicator(
        sortColumn_, sortDescending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
    if (currentPlaylist_.isEmpty()) {
        trackStack_->setCurrentWidget(nonePage_);
        setWindowTitle(QStringLiteral("Playlist Manager"));
    } else {
        setWindowTitle(QStringLiteral("%1 — Playlist Manager").arg(QFileInfo(currentPlaylist_).completeBaseName()));
        trackStack_->setCurrentWidget(tracks_.isEmpty() ? static_cast<QWidget*>(emptyPage_) : trackView_);
    }
    updateActions();
}

void PlaylistWindow::updateActions() {
    const bool hasDirectory = !playlistDirectory().isEmpty();
    const bool hasPlaylist = !currentPlaylist_.isEmpty();
    const bool hasTracks = !selectedRows().isEmpty();
    newAction_->setEnabled(hasDirectory);
    renameAction_->setEnabled(hasPlaylist);
    deleteAction_->setEnabled(hasPlaylist);
    importAction_->setEnabled(hasDirectory);
    exportAction_->setEnabled(hasPlaylist);
    removeAction_->setEnabled(hasTracks);
    moveUpAction_->setEnabled(hasTracks);
    moveDownAction_->setEnabled(hasTracks);
    playAction_->setEnabled(hasPlaylist);
}

void PlaylistWindow::rememberSelection() {
    if (currentPlaylist_.isEmpty())
        return;
    QStringList paths;
    for (int index : selectedRows()) {
        if (index >= 0 && index < tracks_.size())
            paths.append(tracks_.at(index));
    }
    selectionMemory_.insert(currentPlaylist_, paths);
}

QList<int> PlaylistWindow::savedRows(const QString& playlist) const {
    QList<int> rows;
    QSet<int> used;
    for (const QString& path : selectionMemory_.value(playlist)) {
        for (int index = 0; index < tracks_.size(); ++index) {
            if (used.contains(index) || tracks_.at(index) != path)
                continue;
            used.insert(index);
            rows.append(index);
            break;
        }
    }
    return rows;
}

void PlaylistWindow::onPlaylistSelected() {
    if (loading_)
        return;
    rememberSelection();
    const QListWidgetItem* item = playlistList_->currentItem();
    if (item == nullptr) {
        currentPlaylist_.clear();
        tracks_.clear();
        showTracks();
        return;
    }
    currentPlaylist_ = item->data(Qt::UserRole).toString();
    try {
        tracks_ = loadTracks(currentPlaylist_);
    } catch (const AppError& exc) {
        tracks_.clear();
        showError(exc.message());
    }
    showTracks(savedRows(currentPlaylist_));
}

void PlaylistWindow::onColumnSorted(int column) {
    if (column == sortColumn_)
        sortDescending_ = !sortDescending_;
    else {
        sortColumn_ = column;
        sortDescending_ = false;
    }
    showTracks(selectedRows());
}

void PlaylistWindow::onTrackActivated(QTreeWidgetItem* item, int) {
    const QString path = item->data(0, kRolePath).toString();
    if (!path.isEmpty())
        playPaths({path});
}

QList<int> PlaylistWindow::selectedRows() const {
    return trackView_->playlistIndexes(trackView_->selectedItems());
}

void PlaylistWindow::onNewPlaylist() {
    if (playlistDirectory().isEmpty()) {
        toast(QStringLiteral("Choose a playlist folder first."));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"),
                                               QStringLiteral("This becomes the playlist file name."),
                                               QLineEdit::Normal, QString(), &accepted);
    if (accepted)
        createNamedPlaylist(name);
}

void PlaylistWindow::createNamedPlaylist(const QString& name) {
    const QString directory = playlistDirectory();
    if (directory.isEmpty())
        return;
    try {
        const QString path = createPlaylist(directory, name);
        reloadPlaylistsSelecting(path);
    } catch (const AppError& exc) {
        showError(exc.message());
    }
}

void PlaylistWindow::onRenamePlaylist() {
    if (currentPlaylist_.isEmpty())
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename playlist"), QStringLiteral("This becomes the playlist file name."),
        QLineEdit::Normal, QFileInfo(currentPlaylist_).completeBaseName(), &accepted);
    if (accepted)
        renameCurrent(name);
}

void PlaylistWindow::renameCurrent(const QString& name) {
    if (currentPlaylist_.isEmpty())
        return;
    try {
        const QString previous = currentPlaylist_;
        currentPlaylist_ = renamePlaylist(previous, name);
        if (previous != currentPlaylist_) {
            selectionMemory_.insert(currentPlaylist_, selectionMemory_.take(previous));
        }
        reloadPlaylistsSelecting(currentPlaylist_);
    } catch (const AppError& exc) {
        showError(exc.message());
    }
}

void PlaylistWindow::onDeletePlaylist() {
    if (currentPlaylist_.isEmpty())
        return;
    const QString name = QFileInfo(currentPlaylist_).completeBaseName();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete playlist?"),
        QStringLiteral("“%1” will be removed. The audio and video files stay where they are.").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    try {
        deletePlaylist(currentPlaylist_);
    } catch (const AppError& exc) {
        showError(QStringLiteral("Could not delete the playlist.\n%1").arg(exc.message()));
        return;
    }
    selectionMemory_.remove(currentPlaylist_);
    currentPlaylist_.clear();
    reloadPlaylists();
}

void PlaylistWindow::onImportPlaylist() {
    const QString directory = playlistDirectory();
    if (directory.isEmpty()) {
        toast(QStringLiteral("Choose a playlist folder first."));
        return;
    }
    const QString source = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import playlist"), directory,
        QStringLiteral("Playlists (*.m3u8 *.m3u *.pls *.xspf)"));
    if (source.isEmpty())
        return;
    try {
        reloadPlaylistsSelecting(importPlaylist(source, directory));
    } catch (const AppError& exc) {
        showError(exc.message());
    }
}

void PlaylistWindow::onExportPlaylist() {
    if (currentPlaylist_.isEmpty())
        return;
    const QString directory = playlistDirectory();
    const QString initial = directory.isEmpty()
        ? QString()
        : QDir(directory).filePath(filenameForName(QFileInfo(currentPlaylist_).completeBaseName()));
    const QString destination = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export playlist"), initial, QStringLiteral("M3U8 playlist (*.m3u8)"));
    if (destination.isEmpty())
        return;
    try {
        exportPlaylist(currentPlaylist_, destination);
    } catch (const AppError& exc) {
        showError(exc.message());
    }
}

void PlaylistWindow::removeSelectedTracks() {
    const QList<int> rows = selectedRows();
    if (rows.isEmpty() || currentPlaylist_.isEmpty())
        return;
    QString question;
    if (rows.size() == 1) {
        QString name = trackInfo(tracks_.at(rows.first())).title;
        if (name.isEmpty())
            name = QFileInfo(tracks_.at(rows.first())).fileName();
        question = QStringLiteral("Remove “%1” from this playlist?").arg(name);
    } else {
        question = QStringLiteral("Remove %1 items from this playlist?").arg(rows.size());
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Remove from playlist?"),
        QStringLiteral("%1\nThe files stay where they are.").arg(question),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    const QSet<int> indexes(rows.begin(), rows.end());
    QStringList kept;
    for (int index = 0; index < tracks_.size(); ++index) {
        if (!indexes.contains(index))
            kept.append(tracks_.at(index));
    }
    tracks_ = kept;
    writeCurrent();
}

void PlaylistWindow::moveSelected(int delta) {
    const QList<int> rows = selectedRows();
    if (rows.isEmpty() || currentPlaylist_.isEmpty() || delta == 0)
        return;
    if (delta < 0 && rows.first() == 0)
        return;
    if (delta > 0 && rows.last() == tracks_.size() - 1)
        return;
    QList<int> order = rows;
    if (delta > 0)
        std::reverse(order.begin(), order.end());
    for (int index : order) {
        const int swap = index + delta;
        tracks_.swapItemsAt(index, swap);
    }
    QList<int> select;
    select.reserve(rows.size());
    for (int index : rows)
        select.append(index + delta);
    writeCurrent(select);
}

void PlaylistWindow::onPlaylistFiles(const QString& playlist, const QList<QUrl>& urls) {
    const QString path = playlist.isEmpty() ? currentPlaylist_ : playlist;
    if (path.isEmpty()) {
        toast(QStringLiteral("Create a playlist first."));
        return;
    }
    const QStringList media = mediaPathsFromUrls(urls);
    if (media.isEmpty()) {
        toast(QStringLiteral("No audio or video files in that drop."));
        return;
    }
    QTimer::singleShot(0, this, [this, path, media] { finishPlaylistDrop(path, media); });
}

void PlaylistWindow::finishPlaylistDrop(const QString& playlist, const QStringList& paths) {
    try {
        addPaths(playlist, paths);
    } catch (const AppError& exc) {
        showError(exc.message());
        return;
    }
    if (currentPlaylist_ != playlist)
        selectPlaylist(playlist);
}

void PlaylistWindow::onTrackFiles(const QList<QUrl>& urls) {
    if (currentPlaylist_.isEmpty()) {
        toast(QStringLiteral("Create a playlist first."));
        return;
    }
    const QStringList media = mediaPathsFromUrls(urls);
    if (media.isEmpty()) {
        toast(QStringLiteral("No audio or video files in that drop."));
        return;
    }
    const QString playlist = currentPlaylist_;
    QTimer::singleShot(0, this, [this, playlist, media] { finishTrackDrop(playlist, media); });
}

void PlaylistWindow::finishTrackDrop(const QString& playlist, const QStringList& paths) {
    try {
        addPaths(playlist, paths);
    } catch (const AppError& exc) {
        showError(exc.message());
    }
}

void PlaylistWindow::selectPlaylist(const QString& playlist) {
    for (int row = 0; row < playlistList_->count(); ++row) {
        if (playlistList_->item(row)->data(Qt::UserRole).toString() == playlist) {
            playlistList_->setCurrentRow(row);
            return;
        }
    }
}

void PlaylistWindow::addPaths(const QString& playlist, const QStringList& paths) {
    QStringList tracks = loadTracks(playlist);
    const int insertAt = tracks.size();
    for (const QString& path : paths)
        tracks.append(path);
    saveTracks(playlist, tracks);
    if (currentPlaylist_ == playlist) {
        tracks_ = tracks;
        QList<int> select;
        for (int index = 0; index < paths.size(); ++index)
            select.append(insertAt + index);
        showTracks(select);
    }
}

void PlaylistWindow::writeCurrent(const QList<int>& select) {
    if (currentPlaylist_.isEmpty())
        return;
    try {
        saveTracks(currentPlaylist_, tracks_);
    } catch (const AppError& exc) {
        showError(exc.message());
        return;
    }
    showTracks(select);
}

void PlaylistWindow::play() {
    const QList<int> rows = selectedRows();
    if (!rows.isEmpty()) {
        QStringList paths;
        for (int index : rows)
            paths.append(tracks_.at(index));
        playPaths(paths);
        return;
    }
    if (!currentPlaylist_.isEmpty())
        playPaths({currentPlaylist_});
}

void PlaylistWindow::playPaths(const QStringList& paths) {
    if (paths.isEmpty())
        return;
    QString raw = settings_.vlcCommand.trimmed();
    if (raw.isEmpty())
        raw = QStringLiteral("vlc");
    QStringList command;
    try {
        command = splitCommand(raw);
    } catch (const AppError& exc) {
        showError(QStringLiteral("The VLC command could not be read.\n%1").arg(exc.message()));
        return;
    }
    if (command.isEmpty())
        command = {QStringLiteral("vlc")};
    const QString program = command.first();
    QStringList arguments = command.mid(1);
    arguments += paths;
    if (!QProcess::startDetached(program, arguments))
        showError(QStringLiteral("Could not start VLC.\n%1").arg(program));
}

void PlaylistWindow::openAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void PlaylistWindow::openSettings() {
    if (settingsDialog_ == nullptr)
        settingsDialog_ = new SettingsDialog(this);
    else
        settingsDialog_->refresh();
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void PlaylistWindow::chooseFolder() {
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Playlist folder"), settings_.playlistDir.trimmed());
    if (path.isEmpty())
        return;
    settings_.playlistDir = path;
    try {
        saveSettings(settings_);
    } catch (const AppError& exc) {
        showError(QStringLiteral("Could not save settings.\n%1").arg(exc.message()));
        return;
    }
    if (settingsDialog_ != nullptr)
        settingsDialog_->refresh();
    reloadPlaylists();
}

void PlaylistWindow::toast(const QString& message) {
    statusBar()->showMessage(message, 3000);
}

void PlaylistWindow::showError(const QString& message) {
    QMessageBox::critical(this, QStringLiteral("Playlist Manager"), message);
}
