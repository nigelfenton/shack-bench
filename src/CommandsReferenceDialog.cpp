#include "CommandsReferenceDialog.h"

#include "TciCommands.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace TciMon {

namespace {

constexpr const char* kHeaderStyle =
    "QLabel { color: #ffd400; font-size: 16px; font-weight: bold; "
    "font-family: Consolas, 'Cascadia Mono', monospace; }";

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

constexpr const char* kCodeStyle =
    "QLabel { color: #00d8ef; font-size: 12px; "
    "font-family: Consolas, 'Cascadia Mono', monospace; }";

constexpr const char* kBodyStyle =
    "QLabel { color: #dde6f0; font-size: 12px; }";

constexpr int kRoleCmdName = Qt::UserRole + 1;

} // namespace

CommandsReferenceDialog::CommandsReferenceDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("TCI commands reference");
    resize(900, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // Filter row
    {
        auto* row = new QHBoxLayout;
        auto* cap = new QLabel("FILTER");
        cap->setStyleSheet(kCaptionStyle);
        m_filter = new QLineEdit;
        m_filter->setPlaceholderText("Substring match across name, summary, syntax…");
        connect(m_filter, &QLineEdit::textChanged,
                this, &CommandsReferenceDialog::onFilterChanged);
        row->addWidget(cap);
        row->addWidget(m_filter, 1);
        layout->addLayout(row);
    }

    auto* split = new QSplitter(Qt::Horizontal);

    // Left — tree
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setColumnCount(1);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &CommandsReferenceDialog::onSelectionChanged);
    split->addWidget(m_tree);

    // Right — detail pane wrapped in scroll area
    {
        auto* rightWrap = new QWidget;
        auto* rv = new QVBoxLayout(rightWrap);
        rv->setContentsMargins(10, 6, 6, 6);
        rv->setSpacing(8);

        m_titleLabel = new QLabel("—");
        m_titleLabel->setStyleSheet(kHeaderStyle);
        rv->addWidget(m_titleLabel);

        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #1c2a40;");
        rv->addWidget(sep);

        m_summaryLabel = new QLabel;
        m_summaryLabel->setWordWrap(true);
        m_summaryLabel->setStyleSheet(kBodyStyle);
        rv->addWidget(m_summaryLabel);

        auto* synCap = new QLabel("SYNTAX");
        synCap->setStyleSheet(kCaptionStyle);
        rv->addWidget(synCap);

        m_syntaxLabel = new QLabel;
        m_syntaxLabel->setStyleSheet(kCodeStyle);
        m_syntaxLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_syntaxLabel->setWordWrap(true);
        // Long, whitespace-free syntax tokens (e.g.
        // rx_clicked_on_spot:<rx>,<channel>,<call>,<hz>;) would otherwise
        // demand the full token width as minimum, forcing the right pane
        // wider than the viewport and clipping the description column.
        // Ignored horizontal policy lets the layout shrink us; wordWrap
        // then falls back to character-level breaking.
        m_syntaxLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        rv->addWidget(m_syntaxLabel);

        auto* catCap = new QLabel("CATEGORY");
        catCap->setStyleSheet(kCaptionStyle);
        rv->addWidget(catCap);

        m_categoryLabel = new QLabel;
        m_categoryLabel->setStyleSheet(kBodyStyle);
        rv->addWidget(m_categoryLabel);

        auto* sep2 = new QFrame;
        sep2->setFrameShape(QFrame::HLine);
        sep2->setStyleSheet("color: #1c2a40;");
        rv->addWidget(sep2);

        auto* detCap = new QLabel("DETAILS");
        detCap->setStyleSheet(kCaptionStyle);
        rv->addWidget(detCap);

        m_detailsLabel = new QLabel;
        m_detailsLabel->setWordWrap(true);
        m_detailsLabel->setStyleSheet(kBodyStyle);
        m_detailsLabel->setTextInteractionFlags(
            Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_detailsLabel->setAlignment(Qt::AlignTop);
        rv->addWidget(m_detailsLabel);
        rv->addStretch();

        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(rightWrap);
        split->addWidget(scroll);
    }

    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    layout->addWidget(split, 1);

    // Close button
    {
        auto* row = new QHBoxLayout;
        row->addStretch();
        auto* closeBtn = new QPushButton("Close");
        closeBtn->setDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        row->addWidget(closeBtn);
        layout->addLayout(row);
    }

    populateTree();

    // Default selection — first command under first category.
    if (m_tree->topLevelItemCount() > 0) {
        auto* first = m_tree->topLevelItem(0);
        first->setExpanded(true);
        if (first->childCount() > 0) {
            m_tree->setCurrentItem(first->child(0));
        }
    }
}

void CommandsReferenceDialog::populateTree()
{
    m_tree->clear();

    // Build a name→category index from the data, then walk categories
    // in canonical display order.
    QMap<QString, QList<const TciCommand*>> byCategory;
    for (const auto& c : allTciCommands()) {
        byCategory[c.category].append(&c);
    }

    for (const QString& cat : tciCommandCategories()) {
        if (!byCategory.contains(cat)) continue;
        auto* catItem = new QTreeWidgetItem(m_tree);
        catItem->setText(0, cat);
        QFont f = catItem->font(0);
        f.setBold(true);
        catItem->setFont(0, f);
        catItem->setFlags(Qt::ItemIsEnabled);   // not selectable

        for (const TciCommand* c : byCategory.value(cat)) {
            auto* item = new QTreeWidgetItem(catItem);
            item->setText(0, c->name);
            item->setData(0, kRoleCmdName, c->name);
        }
        catItem->setExpanded(true);
    }
}

void CommandsReferenceDialog::onSelectionChanged()
{
    auto sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    auto* item = sel.first();
    const QString name = item->data(0, kRoleCmdName).toString();
    if (name.isEmpty()) return;   // category header

    const TciCommand* cmd = findTciCommand(name);
    if (!cmd) return;

    m_titleLabel->setText(cmd->name);
    m_summaryLabel->setText(cmd->summary);
    m_syntaxLabel->setText(cmd->syntax);
    m_categoryLabel->setText(cmd->category);
    if (cmd->details.isEmpty()) {
        m_detailsLabel->setText(
            QStringLiteral("<i style='color:#6b8099;'>"
                           "No extra detail beyond the summary.</i>"));
        m_detailsLabel->setTextFormat(Qt::RichText);
    } else {
        m_detailsLabel->setTextFormat(Qt::PlainText);
        m_detailsLabel->setText(cmd->details);
    }
}

void CommandsReferenceDialog::onFilterChanged(const QString& text)
{
    const QString needle = text.trimmed().toLower();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* cat = m_tree->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < cat->childCount(); ++j) {
            auto* item = cat->child(j);
            const QString name = item->data(0, kRoleCmdName).toString();
            bool match = needle.isEmpty();
            if (!match) {
                const TciCommand* cmd = findTciCommand(name);
                if (cmd) {
                    match = cmd->name.contains(needle, Qt::CaseInsensitive)
                         || cmd->summary.contains(needle, Qt::CaseInsensitive)
                         || cmd->syntax.contains(needle, Qt::CaseInsensitive)
                         || cmd->details.contains(needle, Qt::CaseInsensitive);
                }
            }
            item->setHidden(!match);
            if (match) ++visibleChildren;
        }
        cat->setHidden(visibleChildren == 0);
        if (!needle.isEmpty() && visibleChildren > 0) {
            cat->setExpanded(true);
        }
    }
}

} // namespace TciMon
