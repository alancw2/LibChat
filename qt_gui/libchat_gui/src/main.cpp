#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LibChat"));
    app.setOrganizationName(QStringLiteral("LibChat"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Load dark stylesheet
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QLatin1String(styleFile.readAll()));
        styleFile.close();
    }

    MainWindow w;
    w.show();
    return app.exec();
}
