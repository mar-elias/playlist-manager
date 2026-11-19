#pragma once

#include <QString>
#include <QStringList>

// POSIX shlex.split: quotes, backslash escapes, and no comment handling.
QStringList splitCommand(const QString& command);
