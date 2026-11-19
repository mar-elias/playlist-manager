#include "shlex.hpp"

#include "error.hpp"

QStringList splitCommand(const QString& command) {
    QStringList tokens;
    QString token;
    bool inToken = false;
    QChar quote;
    bool escape = false;

    for (const QChar character : command) {
        if (escape) {
            if (quote == '"') {
                if (character != '"' && character != '\\')
                    token.append('\\');
            }
            token.append(character);
            inToken = true;
            escape = false;
            continue;
        }
        if (character == '\\' && quote != '\'') {
            escape = true;
            continue;
        }
        if ((character == '\'' || character == '"') && quote.isNull()) {
            quote = character;
            inToken = true;
            continue;
        }
        if (!quote.isNull() && character == quote) {
            quote = QChar();
            inToken = true;
            continue;
        }
        if (quote.isNull() && (character == ' ' || character == '\t' || character == '\n' || character == '\r')) {
            if (inToken) {
                tokens.append(token);
                token.clear();
                inToken = false;
            }
            continue;
        }
        token.append(character);
        inToken = true;
    }

    if (escape)
        throw AppError("No escaped character");
    if (!quote.isNull())
        throw AppError("No closing quotation");
    if (inToken)
        tokens.append(token);
    return tokens;
}
