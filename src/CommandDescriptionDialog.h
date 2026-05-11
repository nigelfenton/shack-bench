#pragma once

// Small popup invoked from the raw-log right-click menu.
// Shows a one-line summary first; if the command has a longer
// description, a "Show full details" button reveals it inline.
//
// For unknown commands (not in the reference table) the dialog
// shows a "no description on file" hint plus the literal text the
// user right-clicked, so they can copy it for a future issue.

#include <QDialog>

class QLabel;
class QPushButton;

namespace TciMon {

struct TciCommand;

class CommandDescriptionDialog : public QDialog {
    Q_OBJECT
public:
    // `cmd` may be nullptr for unknown commands; in that case
    // `cmdName` is shown verbatim.  For known commands `cmdName` is
    // ignored — the canonical name from `cmd` is used.
    CommandDescriptionDialog(const TciCommand* cmd,
                             const QString& cmdName,
                             QWidget* parent = nullptr);

private slots:
    void onShowDetails();

private:
    QLabel*      m_detailsLabel{nullptr};
    QPushButton* m_detailsBtn{nullptr};
    QString      m_detailsText;
};

} // namespace TciMon
