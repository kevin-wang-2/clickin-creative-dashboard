#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include "core/app/Application.h"
#include "providers/core_asset/CoreAssetPlugin.h"
#include "providers/local_file/LocalFilePlugin.h"
#include "providers/local_audio/LocalAudioPlugin.h"
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
    app.addPlugin(std::make_unique<clickin::CoreAssetPlugin>());
    app.addPlugin(std::make_unique<clickin::LocalFilePlugin>());
    app.addPlugin(std::make_unique<clickin::LocalAudioPlugin>());

    if (!app.initialize(dbPath)) {
        return 1;
    }

    MainWindow window(app);
    window.show();

    int result = qtApp.exec();
    app.shutdown();
    return result;
}
