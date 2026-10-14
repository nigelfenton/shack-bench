#include "TxGate.h"

#include <QStringList>

namespace TciMon {

namespace {

// First argument as a receiver index; TCI defaults to receiver 0.
int rxOf(const QStringList& parts)
{
    if (parts.isEmpty()) return 0;
    bool ok = false;
    const int rx = parts.first().trimmed().toInt(&ok);
    return (ok && rx >= 0) ? rx : 0;
}

bool hasFlag(const QStringList& parts, const char* flag)
{
    for (const QString& p : parts)
        if (p.trimmed().compare(QLatin1String(flag), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

} // namespace

QVector<TciCommandPart> splitTciCommands(const QString& text)
{
    QVector<TciCommandPart> out;
    for (const QString& seg : text.split(';', Qt::SkipEmptyParts)) {
        const QString s = seg.trimmed();
        if (s.isEmpty()) continue;
        const int colon = s.indexOf(':');
        TciCommandPart c;
        c.name = (colon < 0 ? s : s.left(colon)).trimmed().toLower();
        c.args = colon < 0 ? QString() : s.mid(colon + 1);
        out.push_back(c);
    }
    return out;
}

bool isCwSend(const QString& name)
{
    return name == "cw_msg" || name == "cw_macros";
}

bool isTxClassCommand(const QString& name)
{
    return name == "trx" || name == "tune" || isCwSend(name);
}

int keyDownRx(const TciCommandPart& c)
{
    const QStringList parts = c.args.split(',', Qt::SkipEmptyParts);
    if (c.name == "trx" || c.name == "tune")
        return hasFlag(parts, "true") ? rxOf(parts) : -1;
    // A CW send keys the transmitter for as long as the text takes.
    if (isCwSend(c.name))
        return parts.size() >= 2 ? rxOf(parts) : -1;
    return -1;
}

int keyUpRx(const TciCommandPart& c)
{
    if (c.name != "trx" && c.name != "tune") return -1;
    const QStringList parts = c.args.split(',', Qt::SkipEmptyParts);
    return hasFlag(parts, "false") ? rxOf(parts) : -1;
}

} // namespace TciMon
