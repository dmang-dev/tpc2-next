// SPDX-License-Identifier: MIT
#pragma once

#include <QMetaType>
#include <QString>

namespace tpc2::gui {

struct BackendConfig {
    enum class Kind {
        Demo = 0,
        Bridge = 1,
        Parallel = 2,
    };

    Kind kind = Kind::Demo;
    QString port;
    int baud = 115200;
    quint16 lptBase = 0x378;
    int mode = 0; // 0 auto, 1 old, 2 new
};

} // namespace tpc2::gui

Q_DECLARE_METATYPE(tpc2::gui::BackendConfig)
