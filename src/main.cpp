// Shack-Bench — entry point.
// Sets up the QApplication, applies the dark stylesheet, opens the main
// monitor window.

#include "MainWindow.h"

#include <QApplication>
#include <QSettings>

namespace {

constexpr const char* kAppStylesheet = R"(
QMainWindow, QDialog, QWidget {
    background-color: #0a1320;
    color: #dde6f0;
    font-family: "Segoe UI", "Helvetica Neue", sans-serif;
    font-size: 11px;
}
QStatusBar { background-color: #0d1e30; color: #6b8099; }
QStatusBar::item { border: none; }
QPushButton {
    background-color: #0f1e30;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 4px 12px;
    color: #dde6f0;
}
QPushButton:hover { background-color: #0e1a2e; border: 1px solid #00d8ef; }
QPushButton:disabled { color: #3a4a60; border: 1px solid #1c2a40; }
QLineEdit, QSpinBox {
    background-color: #0b1220;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 4px 6px;
    color: #dde6f0;
}
QLineEdit:focus, QSpinBox:focus { border: 1px solid #00d8ef; }
QTableWidget {
    background-color: #0a1320;
    alternate-background-color: #0d1e30;
    gridline-color: #1c2a40;
    color: #dde6f0;
    selection-background-color: #00475e;
    selection-color: #00d8ef;
}
QHeaderView::section {
    background-color: #0d1e30;
    color: #6b8099;
    border: 1px solid #1c2a40;
    padding: 4px 6px;
    font-weight: bold;
}
QCheckBox { color: #dde6f0; }
QSplitter::handle { background-color: #1c2a40; }
)";

} // namespace

int main(int argc, char* argv[])
{
    QApplication::setOrganizationName("G0JKN");
    QApplication::setOrganizationDomain("g0jkn.uk");
    QApplication::setApplicationName("Shack-Bench");
    QApplication::setApplicationVersion("0.4.0");

    QApplication app(argc, argv);

    // The app was called "TCI Monitor" until 2026-08-03, and QSettings keys
    // off the application name — so a rename silently orphans every stored
    // setting (window layout, TX-cal parameters, suppressed message types).
    // Carry them across once, on first run under the new name, rather than
    // making the rename cost the user their configuration.
    {
        QSettings fresh;
        if (fresh.allKeys().isEmpty()) {
            // Match the platform default (registry on Windows, INI elsewhere)
            // rather than forcing IniFormat, which would look in the wrong
            // store and silently find nothing.
            QSettings old("G0JKN", "TCI Monitor");
            const QStringList keys = old.allKeys();
            if (!keys.isEmpty()) {
                for (const QString& k : keys) fresh.setValue(k, old.value(k));
                fresh.sync();
                qInfo("migrated %lld settings from the former \"TCI Monitor\" "
                      "identity", static_cast<long long>(keys.size()));
            }
        }
    }
    app.setStyleSheet(kAppStylesheet);

    TciMon::MainWindow w;
    w.show();
    return app.exec();
}
