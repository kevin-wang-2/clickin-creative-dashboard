#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStandardPaths>
#include <QDir>
#include "core/app/Application.h"
#include "providers/core_asset/CoreAssetPlugin.h"
#include "providers/local_file/LocalFilePlugin.h"
#include "providers/local_audio/LocalAudioPlugin.h"
#include "ui/shell/MainWindow.h"

static void applyDarkPalette(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window,          QColor(45,  45,  45));
    p.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    p.setColor(QPalette::Base,            QColor(28,  28,  28));
    p.setColor(QPalette::AlternateBase,   QColor(38,  38,  38));
    p.setColor(QPalette::ToolTipBase,     QColor(28,  28,  28));
    p.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
    p.setColor(QPalette::Text,            QColor(220, 220, 220));
    p.setColor(QPalette::Button,          QColor(55,  55,  55));
    p.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(80, 160, 240));
    p.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, QColor(220, 220, 220));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(110, 110, 110));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 110));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(110, 110, 110));
    app.setPalette(p);
}

int main(int argc, char* argv[]) {
    QApplication qtApp(argc, argv);
    QApplication::setApplicationName("Clickin Creative Dashboard");
    QApplication::setOrganizationName("Clickin Studio");
    QApplication::setApplicationVersion("0.1.0");
    applyDarkPalette(qtApp);

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

    for (const auto& pluginId : app.autoStartWindowPluginIds()) {
        QWidget* win = app.createPluginWindowFor(pluginId, nullptr);
        if (win) {
            win->setAttribute(Qt::WA_DeleteOnClose);
            win->show();
        }
    }

    int result = qtApp.exec();
    app.shutdown();
    return result;
}
