#include "CommandDescriptionDialog.h"

#include "TciCommands.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
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

} // namespace

CommandDescriptionDialog::CommandDescriptionDialog(const TciCommand* cmd,
                                                   const QString& cmdName,
                                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString("TCI command — %1")
                       .arg(cmd ? cmd->name : cmdName));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    // Title row
    auto* title = new QLabel(cmd ? cmd->name : cmdName);
    title->setStyleSheet(kHeaderStyle);
    layout->addWidget(title);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #1c2a40;");
    layout->addWidget(sep);

    if (!cmd) {
        // Unknown command path.
        auto* msg = new QLabel(
            QString("No description on file for <b>%1</b>.<br><br>"
                    "If you know what this command does, please raise an "
                    "issue on the Shack-Bench repo with the syntax and "
                    "a short description.")
                .arg(cmdName.toHtmlEscaped()));
        msg->setWordWrap(true);
        msg->setStyleSheet(kBodyStyle);
        msg->setTextFormat(Qt::RichText);
        layout->addWidget(msg);
    } else {
        // Summary
        auto* summary = new QLabel(cmd->summary);
        summary->setWordWrap(true);
        summary->setStyleSheet(kBodyStyle);
        layout->addWidget(summary);

        // Syntax + category captions
        auto* synCap = new QLabel("SYNTAX");
        synCap->setStyleSheet(kCaptionStyle);
        layout->addWidget(synCap);

        auto* syntax = new QLabel(cmd->syntax);
        syntax->setStyleSheet(kCodeStyle);
        syntax->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(syntax);

        auto* catCap = new QLabel("CATEGORY");
        catCap->setStyleSheet(kCaptionStyle);
        layout->addWidget(catCap);

        auto* cat = new QLabel(cmd->category);
        cat->setStyleSheet(kBodyStyle);
        layout->addWidget(cat);

        // Details — start hidden, revealed by the "Show full details" button.
        if (!cmd->details.isEmpty()) {
            m_detailsText = cmd->details;

            auto* sep2 = new QFrame;
            sep2->setFrameShape(QFrame::HLine);
            sep2->setStyleSheet("color: #1c2a40;");
            layout->addWidget(sep2);

            m_detailsLabel = new QLabel;
            m_detailsLabel->setWordWrap(true);
            m_detailsLabel->setStyleSheet(kBodyStyle);
            m_detailsLabel->setText(m_detailsText);
            m_detailsLabel->setVisible(false);
            m_detailsLabel->setTextInteractionFlags(
                Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            layout->addWidget(m_detailsLabel);
        }
    }

    // Button row
    layout->addSpacing(6);
    auto* btnRow = new QHBoxLayout;
    if (cmd && !cmd->details.isEmpty()) {
        m_detailsBtn = new QPushButton("Show full details");
        connect(m_detailsBtn, &QPushButton::clicked,
                this, &CommandDescriptionDialog::onShowDetails);
        btnRow->addWidget(m_detailsBtn);
    }
    btnRow->addStretch();

    auto* closeBtn = new QPushButton("Close");
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);

    layout->addLayout(btnRow);

    // Reasonable starting size; auto-grows when details are shown.
    resize(440, sizeHint().height());
}

void CommandDescriptionDialog::onShowDetails()
{
    if (!m_detailsLabel) return;
    m_detailsLabel->setVisible(true);
    if (m_detailsBtn) m_detailsBtn->setVisible(false);
    // Let the dialog grow to accommodate the new content.
    adjustSize();
}

} // namespace TciMon
