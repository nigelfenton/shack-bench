#pragma once

// TxGate — classify a free-text TCI line for the console's TX safety gate.
//
// Pure functions, no widgets, so the gate can be unit-tested. The console
// sends whatever the user typed verbatim, which can be SEVERAL commands:
//     vfo:0,0,14074000;trx:0,true;
// ⚠ Classifying only the first command let that line key the radio with no
// confirm and no watchdog. Every command on the line is now classified.

#include <QString>
#include <QVector>

namespace TciMon {

struct TciCommandPart {
    QString name;   // lower-cased, trimmed — "trx"
    QString args;   // raw text after the ':' — "0,true"
};

// Split "a:1,2;b;c:x;" into its commands. Empty segments are dropped.
QVector<TciCommandPart> splitTciCommands(const QString& text);

// Commands that can put RF on the antenna: trx, tune, and the CW senders.
bool isTxClassCommand(const QString& name);

// Receiver this command keys DOWN (carrier up), or -1 if it doesn't.
//   trx:0,true;  tune:1,true;  cw_msg:0,CQ;  cw_macros:0,TEST;
int keyDownRx(const TciCommandPart& c);

// Receiver this command explicitly unkeys (trx/tune ...,false), or -1.
int keyUpRx(const TciCommandPart& c);

// True for the CW senders — an abort also has to stop the keyer.
bool isCwSend(const QString& name);

} // namespace TciMon
