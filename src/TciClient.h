#pragma once

// TciClient — minimal WebSocket client for the TCI protocol.
//
// Unlike a logger or controller's TCI client, this one is intentionally
// dumb: it does not track current freq/mode, does not interpret events,
// does not filter anything.  Every message line received from the server
// is emitted verbatim via rawMessageReceived() so the monitor's UI can
// show / parse / save the full stream.
//
// Auto-reconnect with exponential backoff (1, 2, 5, 10, 30 s) until the
// caller explicitly disconnects.

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;

namespace TciMon {

class TciClient : public QObject {
    Q_OBJECT
public:
    explicit TciClient(QObject* parent = nullptr);
    ~TciClient() override;

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

    // Returns false — and sends nothing — when the link is down. Callers that
    // log "sent" must check it rather than claim a send that never happened.
    bool send(const QString& cmd);   // surface for ad-hoc command testing
    void sendBinary(const QByteArray& frame);   // TCI TX-audio frames

    // Unkey (trx + tune false) the given receivers. Sent now if connected;
    // otherwise QUEUED and sent first thing on the next connect, ahead of
    // `start;`. Returns true only if it went out now. ⚠ An unkey must never
    // be silently dropped — a keyed radio is the one state we can't leave.
    bool sendUnkey(const QSet<int>& receivers = {0});

    // Push anything queued on the socket out now — used on app close, where
    // the socket is torn down before the event loop could write it.
    void flush();

    bool    connected() const { return m_connected; }
    QUrl    currentUrl() const { return m_url; }
    QString lastError() const { return m_lastError; }

signals:
    void connectionChanged(bool connected);
    void rawMessageReceived(const QString& line);
    void errorTextReceived(const QString& text);   // descriptive socket error

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);

    QWebSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    QUrl    m_url;
    bool    m_userInitiatedDisconnect{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};
    QString m_lastError;
    QSet<int> m_pendingUnkey;   // receivers to unkey on the next connect
};

} // namespace TciMon
