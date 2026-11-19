#include "mainwindow.hpp"
#include "settings.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Mar-Elias Playlist Manager"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    PlaylistWindow window(loadSettings());
    window.show();
    return app.exec();
}
