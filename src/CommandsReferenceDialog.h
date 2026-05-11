#pragma once

// Help → TCI Commands… reference browser.
//
// Left pane: tree of every known command, grouped by category.
// Right pane: full details for whatever is selected.
// Top: filter box (substring match against name, summary, or syntax).
//
// All data comes from TciCommands.cpp — the dialog has no opinions
// about content, just lays it out.

#include <QDialog>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace TciMon {

class CommandsReferenceDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandsReferenceDialog(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onFilterChanged(const QString& text);

private:
    void populateTree();

    QLineEdit*   m_filter{nullptr};
    QTreeWidget* m_tree{nullptr};
    QLabel*      m_titleLabel{nullptr};
    QLabel*      m_summaryLabel{nullptr};
    QLabel*      m_syntaxLabel{nullptr};
    QLabel*      m_categoryLabel{nullptr};
    QLabel*      m_detailsLabel{nullptr};
};

} // namespace TciMon
