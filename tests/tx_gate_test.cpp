// tx_gate_test — the console's TX safety gate, and TciClient's queued unkey.
//
// Part 1 is pure: every command on a line must be classified, because the
// console sends the whole line. Part 2 runs a real loopback QWebSocketServer:
// an unkey requested while the link is down must be sent FIRST on reconnect,
// ahead of `start;`, rather than silently dropped.

#include "TciClient.h"
#include "TxGate.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QStringList>
#include <QWebSocket>
#include <QWebSocketServer>
#include <cstdio>
#include <functional>

using namespace TciMon;

static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

// Does any command on the line key a carrier? (What the console asks.)
static bool linekeys(const QString& line)
{
    for (const TciCommandPart& c : splitTciCommands(line))
        if (keyDownRx(c) >= 0) return true;
    return false;
}

static bool lineIsTx(const QString& line)
{
    for (const TciCommandPart& c : splitTciCommands(line))
        if (isTxClassCommand(c.name)) return true;
    return false;
}

static void pump(int ms, const std::function<bool()>& done)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms && !done())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    std::printf("=== splitting ===\n");
    {
        const auto v = splitTciCommands(" vfo:0,0,14074000 ; TRX:0,true; ;start");
        check(v.size() == 3, "three commands, empty segment dropped");
        check(v.size() == 3 && v[1].name == "trx" && v[1].args == "0,true",
              "names are lower-cased, args kept");
        check(v.size() == 3 && v[2].name == "start" && v[2].args.isEmpty(),
              "a bare command has no args");
    }

    std::printf("\n=== compound lines cannot sneak a key past the gate ===\n");
    check(lineIsTx("vfo:0,0,14074000;trx:0,true;"),
          "trx hidden behind vfo is TX-class");
    check(linekeys("vfo:0,0,14074000;trx:0,true;"),
          "trx hidden behind vfo keys the carrier");
    check(linekeys("start;trx:0,true;"), "trx hidden behind start keys");
    check(!linekeys("vfo:0,0,14074000;modulation:0,usb;"),
          "a line with no TX command does not key");

    std::printf("\n=== CW senders are transmit-class ===\n");
    check(lineIsTx("cw_msg:0,CQ TEST;"), "cw_msg is TX-class");
    check(lineIsTx("cw_macros:0,G0JKN;"), "cw_macros is TX-class");
    check(linekeys("cw_macros:0,G0JKN;"), "cw_macros keys the transmitter");
    check(!lineIsTx("cw_macros_speed:25;"), "cw_macros_speed is not TX-class");

    std::printf("\n=== receiver index is tracked ===\n");
    {
        const auto v = splitTciCommands("trx:1,true;tune:0,false;trx:0,true,tci;");
        check(keyDownRx(v[0]) == 1, "trx:1,true keys receiver 1");
        check(keyUpRx(v[1]) == 0, "tune:0,false unkeys receiver 0");
        check(keyDownRx(v[2]) == 0, "trx:0,true,tci keys receiver 0");
        check(keyDownRx(v[1]) == -1, "an unkey is not a key-down");
    }

    std::printf("\n=== an unkey requested while offline is sent on reconnect ===\n");
    {
        QWebSocketServer server("test", QWebSocketServer::NonSecureMode);
        check(server.listen(QHostAddress::LocalHost, 0), "loopback server listens");

        QStringList received;
        QObject::connect(&server, &QWebSocketServer::newConnection, [&] {
            QWebSocket* s = server.nextPendingConnection();
            QObject::connect(s, &QWebSocket::textMessageReceived,
                             [&](const QString& m) { received << m; });
        });

        TciClient tci;
        check(!tci.sendUnkey({0, 1}), "sendUnkey while offline reports NOT sent");
        check(!tci.send("vfo:0,0,7074000;"), "send while offline reports NOT sent");

        tci.connectToServer("127.0.0.1", server.serverPort());
        pump(3000, [&] { return received.contains("start;"); });

        check(received.size() >= 5, "the server received the queued unkey and start");
        const int start = received.indexOf("start;");
        const int unkey0 = received.indexOf("trx:0,false;");
        const int unkey1 = received.indexOf("trx:1,false;");
        check(unkey0 >= 0 && unkey1 >= 0, "both receivers were unkeyed");
        check(unkey0 >= 0 && start > unkey0 && start > unkey1,
              "the unkey went out BEFORE start;");
        check(!received.contains("vfo:0,0,7074000;"),
              "an ordinary command sent offline is not replayed");

        received.clear();
        check(tci.sendUnkey({0}), "sendUnkey while connected reports sent");
        pump(2000, [&] { return received.contains("tune:0,false;"); });
        check(received.contains("trx:0,false;") && received.contains("tune:0,false;"),
              "a connected unkey reaches the server");

        tci.disconnectFromServer();
    }

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
