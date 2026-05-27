#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include "core/app/Application.h"
#include "ui/shell/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication qtApp(argc, argv);
    QApplication::setApplicationName("Clickin Creative Dashboard");
    QApplication::setOrganizationName("Clickin Studio");
    QApplication::setApplicationVersion("0.1.0");

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    std::string dbPath = (dataDir + "/clickin.db").toStdString();

    clickin::Application app;
    if (!app.initialize(dbPath)) {
        return 1;
    }

    MainWindow window(app);
    window.show();

    int result = qtApp.exec();
    app.shutdown();
    return result;
}
