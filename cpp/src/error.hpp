#pragma once

#include <QString>

#include <stdexcept>

class AppError : public std::runtime_error {
public:
    explicit AppError(const QString& message)
        : std::runtime_error(message.toStdString()), message_(message) {}

    QString message() const { return message_; }

private:
    QString message_;
};
