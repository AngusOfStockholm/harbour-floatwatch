#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <sailfishapp.h>

#include "vescbackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication *app = SailfishApp::application(argc, argv);
    QQuickView *view = SailfishApp::createView();

    VescBackend backend;
    view->rootContext()->setContextProperty("vescBackend", &backend);

    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
