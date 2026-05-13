#pragma once

// MdnsBrowser — pure-Qt mDNS / DNS-SD browser for _tci._tcp.local.
//
// Sends periodic PTR queries to 224.0.0.251:5353 and parses responses
// (unicast or multicast) into TciService records.  Matches the schema
// defined in ten9876/AetherSDR/docs/tci-discovery.md.
//
// Design constraints:
//   - Pure Qt (Core + Network).  No QtZeroConf / KDNSSD / OS native APIs.
//   - Bind to an ephemeral port and set the QU bit in queries so
//     responders unicast their replies back to us.  Avoids the platform
//     headaches of binding to UDP 5353 alongside an OS mDNS daemon.
//   - Also joins the 224.0.0.251 multicast group so we additionally
//     receive unsolicited announcements / RFC 6762 §8 cache-priming
//     responses from responders that broadcast in addition to unicast.

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QString>

class QUdpSocket;
class QTimer;

namespace TciMon {

struct TciService {
    QString instance;                 // e.g. "aether_pad G0JKN"
    QString hostname;                 // e.g. "aether-pad-g0jkn.local."
    QHostAddress address;             // resolved IPv4
    quint16 port = 0;                 // SRV port (0 = no TCI server)
    QHash<QString, QString> txt;      // model, class, tci-version, txtvers, x-*
    qint64 lastSeenMs = 0;            // QDateTime::currentMSecsSinceEpoch
};

class MdnsBrowser : public QObject {
    Q_OBJECT
public:
    explicit MdnsBrowser(QObject* parent = nullptr);
    ~MdnsBrowser() override;

    // Bind socket, join multicast group, send first query.  Returns false
    // on bind failure.  Safe to call again after stop().
    bool start();

    // Stop the periodic timer and close the socket.
    void stop();

    // Send a one-off query right now (in addition to the periodic ones).
    void refresh();

    // Service type to browse.  Default "_tci._tcp.local.".  Setting a new
    // value while running clears the discovered set and re-queries.  The
    // trailing dot is added if missing.
    void setServiceType(const QString& type);
    QString serviceType() const { return m_serviceType; }

    // Snapshot of currently-discovered services.
    QList<TciService> services() const;

signals:
    void serviceFound(const TciService& s);    // new instance never seen before
    void serviceUpdated(const TciService& s);  // existing instance, fresh data
    void info(const QString& message);         // for the log pane
    void error(const QString& message);

private slots:
    void onDatagramReady();
    void onPeriodicQuery();

private:
    void sendQuery();
    void parseResponse(const QByteArray& data, const QHostAddress& from);

    // DNS name reader.  Returns dotted form (e.g. "aether-pad-g0jkn.local.").
    // Advances *off* past the name.  Handles compression pointers (max 32 hops).
    QString readName(const QByteArray& d, int& off, int depth = 0) const;

    // DNS-encode a dotted name like "_tci._tcp.local." into wire-format
    // labels (length-prefixed segments + 0 terminator).
    static QByteArray encodeName(const QString& dotted);

    QUdpSocket* m_socket = nullptr;
    QTimer*     m_timer  = nullptr;
    bool        m_running = false;
    QString     m_serviceType = QStringLiteral("_tci._tcp.local.");
    QByteArray  m_serviceNameWire;          // DNS-encoded m_serviceType
    QString     m_instanceSuffix;           // "." + m_serviceType
    QHash<QString, TciService> m_services;  // keyed by instance label
};

} // namespace TciMon
