#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "backend.h"

int main(int argc, char* argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("TakionServerGUI");
    app.setApplicationDisplayName("TakionServerGUI");
    app.setWindowIcon(QIcon(":/app.png"));
    ServerBackend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &ServerBackend::stopServer);
    engine.loadFromModule("PsGui", "Main");
    return app.exec();
}