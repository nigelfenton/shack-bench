#include "MdnsBrowser.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

namespace TciMon {

namespace {

const QHostAddress kMdnsGroup(QStringLiteral("224.0.0.251"));
constexpr quint16  kMdnsPort   = 5353;

constexpr quint16 kTypePTR = 0x000C;
constexpr quint16 kTypeSRV = 0x0021;
constexpr quint16 kTypeTXT = 0x0010;
constexpr quint16 kTypeA   = 0x0001;

constexpr int kPeriodicQueryMs = 5000;     // re-query every 5 s while dialog open
constexpr int kMaxCompressionHops = 32;

void writeU16(QByteArray& b, quint16 v) {
    b.append(char(v >> 8));
    b.append(char(v & 0xFF));
}

} // namespace

// ---------------------------------------------------------------------------

MdnsBrowser::MdnsBrowser(QObject* parent)
    : QObject(parent)
{
    setServiceType(m_serviceType);   // initialise the encoded form
}

MdnsBrowser::~MdnsBrowser() {
    stop();
}

QByteArray MdnsBrowser::encodeName(const QString& dotted) {
    QByteArray out;
    QString s = dotted;
    if (!s.endsWith(QChar('.'))) s += QChar('.');
    int start = 0;
    while (start < s.size()) {
        int dot = s.indexOf(QChar('.'), start);
        if (dot < 0) dot = s.size();
        const QByteArray label = s.mid(start, dot - start).toUtf8();
        if (label.isEmpty()) { start = dot + 1; continue; }
        if (label.size() > 63) return {};   // malformed input — give up
        out.append(char(label.size()));
        out.append(label);
        start = dot + 1;
    }
    out.append(char(0));
    return out;
}

void MdnsBrowser::setServiceType(const QString& type) {
    QString t = type.trimmed();
    if (!t.endsWith(QChar('.'))) t += QChar('.');
    m_serviceType     = t;
    m_serviceNameWire = encodeName(t);
    m_instanceSuffix  = QChar('.') + t;
    m_services.clear();
    if (m_running) sendQuery();
}

bool MdnsBrowser::start() {
    if (m_running) return true;

    m_socket = new QUdpSocket(this);
    // Must bind to UDP 5353 — responders send to that destination port,
    // and the kernel demuxes inbound UDP by (group, port).  A socket
    // bound to an ephemeral port wouldn't see the multicast packets even
    // after joinMulticastGroup() because the destination port wouldn't
    // match.  ShareAddress + ReuseAddressHint lets us co-exist with any
    // OS mDNS daemon (Bonjour service on Windows, mdnsd on macOS, avahi
    // on Linux) that already holds the same port.
    if (!m_socket->bind(QHostAddress::AnyIPv4, kMdnsPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit error(QStringLiteral("mDNS: socket bind to UDP %1 failed — %2")
                       .arg(kMdnsPort)
                       .arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    // Join 224.0.0.251 on every interface that can carry it.  Without
    // this we'd miss responders that multicast their replies (RFC 6762
    // says responders may multicast in addition to / instead of unicast).
    const auto ifaces = QNetworkInterface::allInterfaces();
    int joined = 0;
    for (const auto& iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)   continue;
        if (!(iface.flags() & QNetworkInterface::CanMulticast)) continue;
        if (m_socket->joinMulticastGroup(kMdnsGroup, iface)) joined++;
    }
    if (joined == 0) {
        // Not fatal — unicast replies to our QU-bit queries still work.
        emit info(QStringLiteral("mDNS: no multicast-capable interface joined; "
                                 "unicast replies only"));
    }

    connect(m_socket, &QUdpSocket::readyRead,
            this,     &MdnsBrowser::onDatagramReady);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPeriodicQueryMs);
    connect(m_timer, &QTimer::timeout, this, &MdnsBrowser::onPeriodicQuery);
    m_timer->start();

    m_running = true;
    sendQuery();
    emit info(QStringLiteral("mDNS: browsing %1 on %2 interface(s)")
                  .arg(m_serviceType).arg(joined));
    return true;
}

void MdnsBrowser::stop() {
    if (m_timer)  { m_timer->stop();  m_timer->deleteLater();  m_timer = nullptr; }
    if (m_socket) { m_socket->close(); m_socket->deleteLater(); m_socket = nullptr; }
    m_running = false;
}

void MdnsBrowser::refresh() {
    if (m_running) sendQuery();
}

QList<TciService> MdnsBrowser::services() const {
    return m_services.values();
}

void MdnsBrowser::onPeriodicQuery() {
    sendQuery();
}

// ---------------------------------------------------------------------------
// Query construction
// ---------------------------------------------------------------------------
void MdnsBrowser::sendQuery() {
    if (!m_socket) return;

    QByteArray pkt;
    pkt.reserve(64);

    // Header — QID=0, flags=0 (standard query), QDCOUNT=1, ANCOUNT/NSCOUNT/ARCOUNT=0
    writeU16(pkt, 0x0000);
    writeU16(pkt, 0x0000);
    writeU16(pkt, 0x0001);
    writeU16(pkt, 0x0000);
    writeU16(pkt, 0x0000);
    writeU16(pkt, 0x0000);

    pkt.append(m_serviceNameWire);

    // QTYPE = PTR, QCLASS = IN with QU (top bit) set — ask for unicast
    // replies so we get answers even without 5353 multicast receive.
    writeU16(pkt, kTypePTR);
    writeU16(pkt, 0x8001);

    // Multicast TX on multi-homed boxes: by default the kernel picks ONE
    // outbound interface based on routing.  On Windows this often picks
    // Tailscale / Hyper-V / WSL adapters over the real LAN.  Force the
    // query out every multicast-capable IPv4 interface so the right
    // segment definitely sees it.
    int sentCount = 0;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))         continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning))    continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)      continue;
        if (!(iface.flags() & QNetworkInterface::CanMulticast)) continue;

        bool hasIPv4 = false;
        QHostAddress v4;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                hasIPv4 = true;
                v4 = entry.ip();
                break;
            }
        }
        if (!hasIPv4) continue;

        m_socket->setMulticastInterface(iface);
        (void)v4;
        if (m_socket->writeDatagram(pkt, kMdnsGroup, kMdnsPort) >= 0)
            sentCount++;
    }
    if (sentCount == 0) {
        emit error(QStringLiteral("mDNS: no multicast-capable interface to send on"));
    }
}

// ---------------------------------------------------------------------------
// Receive + parse
// ---------------------------------------------------------------------------
void MdnsBrowser::onDatagramReady() {
    if (!m_socket) return;
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_socket->receiveDatagram();
        if (dg.data().size() < 12) continue;
        parseResponse(dg.data(), dg.senderAddress());
    }
}

QString MdnsBrowser::readName(const QByteArray& d, int& off, int depth) const {
    if (depth > kMaxCompressionHops) return {};
    QStringList labels;
    while (off < d.size()) {
        quint8 len = quint8(d[off]);
        if (len == 0) {
            off += 1;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (off + 1 >= d.size()) return {};
            int ptr = ((len & 0x3F) << 8) | quint8(d[off + 1]);
            off += 2;
            int sub = ptr;
            labels << readName(d, sub, depth + 1);
            // Pointer terminates the name in this branch.
            QString name = labels.join(QChar('.'));
            if (!name.endsWith(QChar('.'))) name += QChar('.');
            return name;
        }
        if (off + 1 + len > d.size()) return {};
        labels << QString::fromUtf8(d.constData() + off + 1, len);
        off += 1 + len;
    }
    QString name = labels.join(QChar('.'));
    if (!name.isEmpty() && !name.endsWith(QChar('.'))) name += QChar('.');
    return name;
}

void MdnsBrowser::parseResponse(const QByteArray& d, const QHostAddress& /*from*/) {
    if (d.size() < 12) return;
    quint16 flags = (quint8(d[2]) << 8) | quint8(d[3]);
    if ((flags & 0x8000) == 0) return;   // not a response

    quint16 qdcount = (quint8(d[4])  << 8) | quint8(d[5]);
    quint16 ancount = (quint8(d[6])  << 8) | quint8(d[7]);
    quint16 nscount = (quint8(d[8])  << 8) | quint8(d[9]);
    quint16 arcount = (quint8(d[10]) << 8) | quint8(d[11]);

    int off = 12;

    // Skip questions (mDNS responses usually have qdcount=0, but be safe).
    for (int q = 0; q < qdcount && off < d.size(); ++q) {
        (void)readName(d, off);
        off += 4;     // QTYPE + QCLASS
    }

    // Accumulate per-instance data as we walk Answer + Authority + Additional.
    struct Pending {
        QString instance;        // owner of SRV/TXT/PTR-rdata
        QString hostname;        // SRV target
        quint16 port = 0;
        QHash<QString, QString> txt;
    };
    QHash<QString, Pending> pending;            // keyed by instance name
    QHash<QString, QHostAddress> aRecords;      // keyed by hostname

    auto recordName = [&](const QString& service, const QString& instance) {
        if (!pending.contains(instance)) {
            Pending p;
            p.instance = instance;
            pending.insert(instance, p);
        }
        (void)service;
    };

    const int total = int(ancount) + int(nscount) + int(arcount);
    for (int i = 0; i < total && off + 10 <= d.size(); ++i) {
        QString rname = readName(d, off);
        if (off + 10 > d.size()) return;
        quint16 type   = (quint8(d[off])   << 8) | quint8(d[off + 1]);
        // class  = (quint8(d[off+2]) << 8) | quint8(d[off+3]);
        // ttl    = 4 bytes at off+4..off+7
        quint16 rdlen  = (quint8(d[off + 8]) << 8) | quint8(d[off + 9]);
        off += 10;
        int rdStart = off;
        int rdEnd   = off + rdlen;
        if (rdEnd > d.size()) return;

        switch (type) {
        case kTypePTR: {
            // Owner must be exactly the service type we're browsing.
            if (rname != m_serviceType) break;
            int p = rdStart;
            QString fullInstance = readName(d, p);   // "<inst>._tci._tcp.local."

            // DNS-SD service-type meta-query: when m_serviceType is
            // _services._dns-sd._udp.local., the PTR rdata is itself a
            // service type (e.g. "_airplay._tcp.local."), not an instance
            // under our type.  Treat the whole name as the "instance" so
            // each discovered type lands as one table row.
            const bool isMetaQuery =
                m_serviceType == QStringLiteral("_services._dns-sd._udp.local.");

            QString instance = fullInstance;
            if (isMetaQuery) {
                // Use the service-type name verbatim.
            } else if (instance.endsWith(m_instanceSuffix)) {
                instance.chop(m_instanceSuffix.size());
            } else {
                break;                               // not our service type
            }
            recordName(rname, instance);
            break;
        }
        case kTypeSRV: {
            if (rdlen < 7) break;
            // Owner must be "<instance>." + service type.  Otherwise this
            // SRV belongs to some other mDNS service (Hue, AirPlay, …) and
            // must not be merged into our pending map.
            if (!rname.endsWith(m_instanceSuffix)) break;
            QString instance = rname;
            instance.chop(m_instanceSuffix.size());

            quint16 port = (quint8(d[rdStart + 4]) << 8) | quint8(d[rdStart + 5]);
            int p = rdStart + 6;
            QString target = readName(d, p);

            Pending& pe = pending[instance];
            pe.instance = instance;
            pe.hostname = target;
            pe.port     = port;
            break;
        }
        case kTypeTXT: {
            if (!rname.endsWith(m_instanceSuffix)) break;
            QString instance = rname;
            instance.chop(m_instanceSuffix.size());

            Pending& pe = pending[instance];
            pe.instance = instance;
            int p = rdStart;
            while (p < rdEnd) {
                quint8 elen = quint8(d[p++]);
                if (p + elen > rdEnd) break;
                QString entry = QString::fromUtf8(d.constData() + p, elen);
                p += elen;
                int eq = entry.indexOf(QChar('='));
                if (eq < 0) pe.txt.insert(entry, QString());
                else        pe.txt.insert(entry.left(eq), entry.mid(eq + 1));
            }
            break;
        }
        case kTypeA: {
            if (rdlen == 4) {
                quint32 ip = (quint8(d[rdStart])     << 24) |
                             (quint8(d[rdStart + 1]) << 16) |
                             (quint8(d[rdStart + 2]) <<  8) |
                              quint8(d[rdStart + 3]);
                aRecords.insert(rname, QHostAddress(ip));
            }
            break;
        }
        default:
            break;
        }
        off = rdEnd;
    }

    // Resolve and emit.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
        const Pending& p = it.value();
        if (p.instance.isEmpty()) continue;

        TciService s;
        s.instance   = p.instance;
        s.hostname   = p.hostname;
        s.port       = p.port;
        s.txt        = p.txt;
        s.lastSeenMs = nowMs;
        if (!p.hostname.isEmpty() && aRecords.contains(p.hostname))
            s.address = aRecords.value(p.hostname);

        bool isNew = !m_services.contains(s.instance);
        m_services.insert(s.instance, s);
        if (isNew) emit serviceFound(s);
        else       emit serviceUpdated(s);
    }
}

} // namespace TciMon
