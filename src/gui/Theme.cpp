// SPDX-License-Identifier: MIT
#include "gui/Theme.h"

#include <QFont>
#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QWidget>

namespace tpc2::gui::Theme {

QString styleSheet()
{
    return QStringLiteral(R"QSS(
QWidget {
    color: #e8eaf2;
    font-size: 10pt;
}
QMainWindow, QWidget#Root, QWidget#Page {
    background: #0b0d13;
}
QFrame[card="true"] {
    background: #12151e;
    border: 1px solid #252b3b;
    border-radius: 14px;
}
QFrame[card="true"][accent="true"] {
    border-color: #3b3567;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #151326, stop:1 #12151e);
}
QLabel[role="h1"] {
    font-size: 20pt;
    font-weight: 700;
    color: #f5f6fb;
}
QLabel[role="h2"] {
    font-size: 12.5pt;
    font-weight: 650;
    color: #f1f2f8;
}
QLabel[role="sub"] {
    color: #8d93a6;
    font-size: 9.5pt;
}
QLabel[role="tiny"] {
    color: #71788c;
    font-size: 8.5pt;
}
QLabel[role="mono"] {
    font-family: "Cascadia Mono", "Consolas", "DejaVu Sans Mono", monospace;
    color: #aeb4c8;
}
QLabel[role="pill"] {
    background: #1a1f2d;
    border: 1px solid #2a3144;
    border-radius: 10px;
    padding: 3px 10px;
    font-size: 8.5pt;
    font-weight: 600;
    color: #9ea5ba;
}
QLabel[role="pill"][state="on"] {
    background: #102a22;
    border-color: #1f5a45;
    color: #5eead4;
}
QLabel[role="pill"][state="busy"] {
    background: #2a2412;
    border-color: #66501c;
    color: #fbbf24;
}
QLabel[role="tag"] {
    background: #1c1735;
    border: 1px solid #3a2f66;
    border-radius: 7px;
    padding: 2px 8px;
    color: #c4b5fd;
    font-size: 8pt;
    font-weight: 600;
}
QPushButton {
    background: #1a2030;
    border: 1px solid #2c3347;
    border-radius: 8px;
    padding: 7px 14px;
    color: #dfe2ee;
}
QPushButton:hover {
    background: #222a3d;
    border-color: #3a4562;
}
QPushButton:pressed {
    background: #161b28;
}
QPushButton[accent="true"] {
    background: #7c5cff;
    border-color: #8b6bff;
    color: #ffffff;
    font-weight: 600;
}
QPushButton[accent="true"]:hover {
    background: #8b6eff;
}
QPushButton[accent="true"]:pressed {
    background: #6a4bea;
}
QPushButton[danger="true"] {
    background: #2a1518;
    border-color: #5a2a30;
    color: #fca5a5;
}
QPushButton[danger="true"]:hover {
    background: #3a1a1f;
}
QPushButton:disabled {
    background: #12161f;
    border-color: #1f2433;
    color: #5a6072;
}
QCheckBox {
    color: #c9cede;
    spacing: 8px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    border: 1px solid #3a4260;
    border-radius: 4px;
    background: #0d1018;
}
QCheckBox::indicator:hover {
    border-color: #7c5cff;
}
QCheckBox::indicator:checked {
    background: #7c5cff;
    border-color: #8b6bff;
}
QComboBox, QLineEdit, QSpinBox {
    background: #0d1018;
    border: 1px solid #2a3042;
    border-radius: 7px;
    padding: 6px 9px;
    color: #e6e8f0;
    selection-background-color: #7c5cff;
    selection-color: #ffffff;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover {
    border-color: #3a4260;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus {
    border-color: #7c5cff;
}
QComboBox::drop-down {
    border: none;
    width: 26px;
}
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #8d93a6;
    margin-right: 9px;
}
QComboBox QAbstractItemView {
    background: #12151e;
    border: 1px solid #2a3042;
    selection-background-color: #2b2554;
    selection-color: #ffffff;
    outline: none;
    padding: 4px;
}
QTableWidget {
    background: #0d1018;
    alternate-background-color: #10141f;
    border: 1px solid #252b3b;
    border-radius: 10px;
    gridline-color: #1d2230;
    selection-background-color: #241d46;
    selection-color: #ffffff;
    outline: none;
}
QTableWidget::item {
    padding: 6px;
    border: none;
}
QHeaderView::section {
    background: #151a26;
    color: #9299ad;
    border: none;
    border-bottom: 1px solid #252b3b;
    padding: 7px;
    font-size: 8.5pt;
    font-weight: 600;
}
QTabWidget::pane {
    border: none;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #8d93a6;
    padding: 9px 17px;
    margin-right: 4px;
    border-radius: 8px;
    font-weight: 600;
}
QTabBar::tab:hover {
    color: #d8dbea;
    background: #141824;
}
QTabBar::tab:selected {
    color: #ffffff;
    background: #1b2030;
}
QProgressBar {
    background: #0d1018;
    border: 1px solid #252b3b;
    border-radius: 7px;
    min-height: 12px;
    max-height: 12px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #7c5cff, stop:1 #22d3ee);
    border-radius: 6px;
}
QPlainTextEdit {
    background: #090b10;
    border: 1px solid #252b3b;
    border-radius: 10px;
    color: #b9c0d4;
    font-family: "Cascadia Mono", "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 9pt;
    selection-background-color: #2b2554;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #2a3144;
    border-radius: 4px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: #3a4562;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: #2a3144;
    border-radius: 4px;
    min-width: 24px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}
QDockWidget {
    color: #c9cede;
    font-weight: 600;
}
QDockWidget::title {
    background: #0e1119;
    padding: 8px 12px;
    border-bottom: 1px solid #252b3b;
}
QStatusBar {
    background: #0e1119;
    border-top: 1px solid #1c2231;
    color: #8d93a6;
}
QStatusBar::item {
    border: none;
}
QMenuBar {
    background: #0b0d13;
    color: #b8bece;
}
QMenuBar::item:selected {
    background: #1b2030;
    border-radius: 5px;
}
QMenu {
    background: #12151e;
    border: 1px solid #2a3042;
    padding: 5px;
}
QMenu::item {
    padding: 7px 26px 7px 12px;
    border-radius: 5px;
}
QMenu::item:selected {
    background: #241d46;
}
QToolTip {
    background: #1a2030;
    color: #e8eaf2;
    border: 1px solid #3a4260;
    padding: 5px;
}
QSplitter::handle {
    background: transparent;
}
)QSS");
}

void apply(QApplication& app)
{
    app.setStyle(QStringLiteral("Fusion"));

    QFont font;
    const QStringList families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Segoe UI Variable Text")))
        font = QFont(QStringLiteral("Segoe UI Variable Text"), 10);
    else if (families.contains(QStringLiteral("Segoe UI")))
        font = QFont(QStringLiteral("Segoe UI"), 10);
    else if (families.contains(QStringLiteral("Inter")))
        font = QFont(QStringLiteral("Inter"), 10);
    else if (families.contains(QStringLiteral("DejaVu Sans")))
        font = QFont(QStringLiteral("DejaVu Sans"), 10);
    app.setFont(font);
    app.setStyleSheet(styleSheet());
}

QIcon appIcon()
{
    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, 256, 256);
    gradient.setColorAt(0.0, QColor("#8b5cf6"));
    gradient.setColorAt(1.0, QColor("#4f46e5"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(12, 12, 232, 232), 54, 54);

    painter.setBrush(QColor("#0b0d13"));
    painter.drawRoundedRect(QRectF(48, 84, 160, 88), 22, 22);

    QFont font = painter.font();
    font.setPixelSize(44);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    painter.setFont(font);
    painter.setPen(QColor("#f5f3ff"));
    painter.drawText(QRectF(48, 84, 160, 88), Qt::AlignCenter, QStringLiteral("V64"));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#22d3ee"));
    painter.drawEllipse(QPointF(196, 62), 13, 13);

    painter.end();
    return QIcon(pixmap);
}

void repolish(QWidget* widget)
{
    if (!widget)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace tpc2::gui::Theme
