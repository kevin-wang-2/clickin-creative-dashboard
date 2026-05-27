#include <QApplication>
#include "core/app/Application.h"
#include "ui/shell/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication qtApp(argc, argv);
    QApplication::setApplicationName("Clickin Creative Dashboard");
    QApplication::setOrganizationName("Clickin Studio");
    QApplication::setApplicationVersion("0.1.0");

    clickin::Application app;
    if (!app.initialize()) {
        return 1;
    }

    MainWindow window(app);
    window.show();

    int result = qtApp.exec();
    app.shutdown();
    return result;
}
