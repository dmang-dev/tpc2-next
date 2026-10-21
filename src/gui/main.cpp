// SPDX-License-Identifier: MIT
#include "gui/MainWindow.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("TPC2 Next"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tpc2next.local"));
    QCoreApplication::setApplicationName(QStringLiteral("TPC2 Next"));
    QCoreApplication::setApplicationVersion(QStringLiteral(TPC2_VERSION));

    tpc2::gui::Theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Doctor V64 upload, download and remote-control workstation."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption demoOption(
        QStringList { QStringLiteral("d"), QStringLiteral("demo") },
        QStringLiteral("Connect to the built-in V64 simulator at startup."));
    QCommandLineOption pageOption(
        QStringList { QStringLiteral("p"), QStringLiteral("page") },
        QStringLiteral("Page to show: transfer (default) or remote."),
        QStringLiteral("page"), QStringLiteral("transfer"));
    QCommandLineOption screenshotOption(
        QStringList { QStringLiteral("s"), QStringLiteral("screenshot") },
        QStringLiteral("Render the window to a PNG file and exit (for documentation)."),
        QStringLiteral("file"));
    QCommandLineOption addOption(
        QStringList { QStringLiteral("add") },
        QStringLiteral("Add a file to the send queue at startup. May be repeated."),
        QStringLiteral("file"));

    parser.addOption(demoOption);
    parser.addOption(pageOption);
    parser.addOption(screenshotOption);
    parser.addOption(addOption);
    parser.process(app);

    tpc2::gui::MainWindow window;

    const QString page = parser.value(pageOption).trimmed().toLower();
    if (page == QStringLiteral("remote"))
        window.showPage(1);
    else if (page != QStringLiteral("transfer") && !page.isEmpty())
        qWarning() << "Unknown page" << page << "- showing the transfer page.";

    if (parser.isSet(screenshotOption))
        window.resize(1200, 880);

    window.show();

    if (parser.isSet(addOption))
        window.addFiles(parser.values(addOption));

    if (parser.isSet(demoOption))
        QTimer::singleShot(50, &window, &tpc2::gui::MainWindow::connectDemo);

    if (parser.isSet(screenshotOption)) {
        const QString path =
            QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QTimer::singleShot(1400, &app, [&window, path] {
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
                qWarning() << "Cannot create the screenshot directory for" << path;
                QCoreApplication::exit(2);
                return;
            }
            if (!window.grab().save(path)) {
                qWarning() << "Cannot save screenshot to" << path;
                QCoreApplication::exit(2);
                return;
            }
            qInfo() << "Saved screenshot to" << path;
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
