#include "TciClient.h"

#include <QWebSocket>
#include <QTimer>
#include <QStringList>

namespace TciMon {

namespace {
constexpr int kBackoff[] = { 1, 2, 5, 10, 30 };
int backoffSeconds(int attempt) {
    const int n = static_cast<int>(sizeof(kBackoff) / sizeof(kBackoff[0]));
    if (attempt < 0) attempt = 0;
    return kBackoff[attempt < n ? attempt : n - 1];
}
} // namespace

TciClient::TciClient(QObject* parent)
    : QObject(parent),
      m_socket(new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this)),
      m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QWebSocket::connected,           this, &TciClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,        this, &TciClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &TciClient::onTextMessage);
    connect(m_socket, &QWebSocket::errorOccurred,       this, &TciClient::onErrorOccurred);

    connect(m_reconnectTimer, &QTimer::timeout, this, &TciClient::onReconnectTimeout);
}

TciClient::~TciClient()
{
    cancelReconnect();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void TciClient::connectToServer(const QString& host, quint16 port)
{
    m_userInitiatedDisconnect = false;
    m_reconnectAttempts = 0;

    QUrl url;
    url.setScheme("ws");
    url.setHost(host);
    url.setPort(port);
    m_url = url;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::disconnectFromServer()
{
    m_userInitiatedDisconnect = true;
    cancelReconnect();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    setConnected(false);
}

void TciClient::send(const QString& cmd)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendTextMessage(cmd);
    }
}

void TciClient::sendBinary(const QByteArray& frame)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendBinaryMessage(frame);
    }
}

void TciClient::onConnected()
{
    m_reconnectAttempts = 0;
    setConnected(true);
    // Send `start;` to request streaming events.  Servers ignore unknown
    // commands so this is safe across TCI dialects.
    send("start;");
    // Opt in to sensor telemetry. tx_sensors carries SWR (and fwd/peak
    // watts, mic dBm) while transmitting — needed to observe an antenna
    // SWR sweep over TCI. rx_sensors adds the per-channel S-meter dBm.
    // Both are opt-in on AetherSDR (default off); unknown elsewhere = safely
    // ignored, same as `start;`.
    send("tx_sensors_enable:true;");
    send("rx_sensors_enable:true;");
}

void TciClient::onDisconnected()
{
    setConnected(false);
    if (!m_userInitiatedDisconnect) scheduleReconnect();
}

void TciClient::onErrorOccurred()
{
    if (m_socket) {
        m_lastError = m_socket->errorString();
        emit errorTextReceived(m_lastError);
    }
}

void TciClient::onTextMessage(const QString& message)
{
    // Servers may concatenate multiple commands into one frame separated
    // by ';' — split so each line is reported individually.
    const QStringList lines = message.split(';', Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (!line.isEmpty()) emit rawMessageReceived(line);
    }
}

void TciClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect) return;
    const int secs = backoffSeconds(m_reconnectAttempts);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(secs * 1000);
}

void TciClient::cancelReconnect()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
}

void TciClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_url.isEmpty()) return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectionChanged(c);
}

} // namespace TciMon
