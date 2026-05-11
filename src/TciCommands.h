#pragma once

// TCI command reference table — single source of truth for command
// names, syntax, and human-readable descriptions used by:
//   • right-click "Description of '<cmd>'…" popup
//   • Suppression dialog (description column)
//   • Help → TCI Commands… reference browser
//
// To add or refine an entry, edit the data table in TciCommands.cpp —
// no other file should need to change.

#include <QString>
#include <QStringList>
#include <vector>

namespace TciMon {

struct TciCommand {
    QString name;       // canonical lowercase, e.g. "vfo"
    QString category;   // display group, e.g. "Frequency / mode"
    QString syntax;     // example syntax, e.g. "vfo:<rx>,<vfo>,<hz>;"
    QString summary;    // one-line plain English
    QString details;    // optional longer text — empty = no "Show more"
};

// Case-insensitive lookup. Returns nullptr for unknown / vendor-specific
// commands; callers should present a "no description on file" message.
const TciCommand* findTciCommand(const QString& name);

// All known commands, grouped by category in display order.
const std::vector<TciCommand>& allTciCommands();

// Category names in canonical display order.
QStringList tciCommandCategories();

} // namespace TciMon
