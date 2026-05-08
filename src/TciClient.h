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

#include <QObject>
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

    void send(const QString& cmd);   // surface for ad-hoc command testing

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
};

} // namespace TciMon
