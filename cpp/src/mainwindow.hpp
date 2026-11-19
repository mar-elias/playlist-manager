#pragma once

#include "settings.hpp"
#include "tags.hpp"

#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QUrl>

class QAction;
class QLabel;
class QLineEdit;
class QSplitter;
class QStackedWidget;
class QTimer;

inline constexpr int kRoleIndex = Qt::UserRole;
inline constexpr int kRolePath = Qt::UserRole + 1;
inline constexpr int kRoleVideo = Qt::UserRole + 2;

class PlaylistWindow;

class PlaylistList : public QListWidget {
    Q_OBJECT

public:
    explicit PlaylistList(QWidget* parent = nullptr);

signals:
    void filesDropped(const QString& playlist, const QList<QUrl>& urls);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

class TitleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    static constexpr int kIcon = 16;
    static constexpr int kGap = 6;
};

class TrackView : public QTreeWidget {
    Q_OBJECT

public:
    explicit TrackView(QWidget* parent = nullptr);

    QList<int> playlistIndexes(const QList<QTreeWidgetItem*>& items) const;

signals:
    void filesDropped(const QList<QUrl>& urls);
    void removeRequested();
    void moveRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

class FileDropPage : public QWidget {
    Q_OBJECT

public:
    explicit FileDropPage(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);

signals:
    void filesDropped(const QList<QUrl>& urls);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QLabel* label_ = nullptr;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(PlaylistWindow* window);

    void refresh();

private slots:
    void chooseFolder();
    void accept() override;

private:
    PlaylistWindow* window_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLineEdit* vlc_ = nullptr;
};

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

class PlaylistWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PlaylistWindow(Settings settings, QWidget* parent = nullptr);

    Settings& settings() { return settings_; }
    void showError(const QString& message);
    void reloadPlaylists();
    void reloadPlaylistsSelecting(const QString& path);

private slots:
    void onPlaylistSelected();
    void onPlaylistFiles(const QString& playlist, const QList<QUrl>& urls);
    void onTrackFiles(const QList<QUrl>& urls);
    void onColumnSorted(int column);
    void onTrackActivated(QTreeWidgetItem* item, int column);
    void removeSelectedTracks();
    void moveSelected(int delta);
    void copySelectedTracks();
    void pasteCopiedTracks();
    void showTrackMenu(const QPoint& position);
    void showPlaylistMenu(const QPoint& position);
    void onNewPlaylist();
    void onRenamePlaylist();
    void onDeletePlaylist();
    void onImportPlaylist();
    void onExportPlaylist();
    void play();
    void refresh();
    void openSettings();
    void openAbout();
    void chooseFolder();

private:
    void buildActions();
    void buildToolbar();
    QAction* makeAction(const QString& text, const QIcon& icon, const QKeySequence& shortcut);
    QString playlistDirectory() const;
    void reloadPlaylists(bool useGiven, const QString& given);
    void showSetup();
    void applyColumnWidths();
    void onColumnResized();
    void writeColumnWidths();
    TrackInfo trackInfo(const QString& path);
    QList<int> displayOrder() const;
    void showTracks(const QList<int>& select = {});
    void updateActions();
    QList<int> selectedRows() const;
    void rememberSelection();
    QList<int> savedRows(const QString& playlist) const;
    void createNamedPlaylist(const QString& name);
    void renameCurrent(const QString& name);
    void finishPlaylistDrop(const QString& playlist, const QStringList& paths);
    void finishTrackDrop(const QString& playlist, const QStringList& paths);
    void selectPlaylist(const QString& playlist);
    void pasteInto(const QString& playlist);
    void addPaths(const QString& playlist, const QStringList& paths);
    void writeCurrent(const QList<int>& select = {});
    void playPaths(const QStringList& paths);
    void toast(const QString& message);

    Settings settings_;
    QString currentPlaylist_;
    QStringList tracks_;
    QStringList copiedTracks_;
    QHash<QString, QStringList> selectionMemory_;
    bool loading_ = false;
    bool applyingColumns_ = false;
    int sortColumn_ = 0;
    bool sortDescending_ = false;
    QHash<QString, QPair<qint64, TrackInfo>> tagCache_;
    SettingsDialog* settingsDialog_ = nullptr;
    QTimer* columnSave_ = nullptr;

    QAction* newAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* playAction_ = nullptr;
    QAction* renameAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* removeAction_ = nullptr;
    QAction* moveUpAction_ = nullptr;
    QAction* moveDownAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* settingsAction_ = nullptr;

    PlaylistList* playlistList_ = nullptr;
    TrackView* trackView_ = nullptr;
    FileDropPage* emptyPage_ = nullptr;
    FileDropPage* nonePage_ = nullptr;
    FileDropPage* setupPage_ = nullptr;
    QStackedWidget* trackStack_ = nullptr;
    QStackedWidget* mainStack_ = nullptr;
    QSplitter* mainPane_ = nullptr;
};
