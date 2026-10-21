// SPDX-License-Identifier: MIT
#pragma once

#include <QApplication>
#include <QIcon>
#include <QString>

namespace tpc2::gui::Theme {

void apply(QApplication& app);
QIcon appIcon();
QString styleSheet();
void repolish(QWidget* widget);

} // namespace tpc2::gui::Theme
