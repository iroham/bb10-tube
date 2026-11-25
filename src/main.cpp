// Native BB10 video client: launcher. The UI is QML (assets/), the service side is the C core (core/), started here
// inside the same process; QML reaches it over 127.0.0.1 at the address in the context property "coreUrl".
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/ForeignWindowControl>
#include <QDebug>
#include <QDir>
#include <QVariant>
#include <QTimer>
#include <QtDeclarative/qdeclarative.h>
#include <QtDeclarative/QDeclarativeContext>
#include <screen/screen.h>
#include <stdio.h>

using namespace bb::cascades;

extern "C" int tube_core_start(const char *data_dir, const char *js_dir, char *url, size_t urlsz);

static void log_msg(QtMsgType, const char *m) { fprintf(stderr, "ui: %s\n", m); }

// A way for QML to reach native code without generated meta-object code: QML sets objectName to a command and
// calls start(0); the timer fires here.
//   "front"  : put the video window over the app's own window (while the app is a tile, so the tile shows the video)
//   "behind" : put it back under the app's window (the normal place: the controls are drawn over it)
class NativeHook : public QTimer {
public:
    NativeHook() : pane(0), savedZ(0), raised(false) { setSingleShot(true); }
    AbstractPane *pane;
protected:
    void timerEvent(QTimerEvent *) {
        stop();
        QByteArray cmd = objectName().toLatin1();
        ForeignWindowControl *fw = pane ? pane->findChild<ForeignWindowControl *>("tubeVideo") : 0;
        screen_window_t win = fw && fw->isBoundToWindow() ? fw->windowHandle() : 0;
        int z = 0, rc = -1;
        if (win) {
            screen_get_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);
            if (cmd == "front" && !raised) { savedZ = z; z = 5; raised = true; rc = screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z); }
            else if (cmd == "behind" && raised) { z = savedZ; raised = false; rc = screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z); }
            if (rc == 0) {
                screen_context_t ctx = 0;
                if (!screen_get_window_property_pv(win, SCREEN_PROPERTY_CONTEXT, (void **)&ctx) && ctx) screen_flush_context(ctx, 0);
            }
        } else if (cmd == "behind") raised = false;       // the window went away meanwhile
        fprintf(stderr, "native: %s: window %s, z now %d (set: %s)\n", cmd.constData(), win ? "bound" : "none", z, rc == 0 ? "yes" : "no");
    }
private:
    int savedZ;
    bool raised;
};

Q_DECL_EXPORT int main(int argc, char **argv) {
    qputenv("CASCADES_THEME", "dark");
    QString root = QDir::currentPath();                       // the app's sandbox
    QByteArray data = (root + "/data").toUtf8(), js = (root + "/app/native/js").toUtf8();
    // one log for the UI and the core, started fresh each run (no tokens are ever logged)
    if (freopen((data + "/app.log").constData(), "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
    qInstallMsgHandler(log_msg);
    Application app(argc, argv);
    char url[200] = "";
    if (tube_core_start(data.constData(), js.constData(), url, sizeof url)) qWarning() << "core did not start";
    qmlRegisterType<QTimer>("tube", 1, 0, "QTimer");          // Qt's timer for QML (already has its meta object)
    NativeHook *hook = new NativeHook();
    QmlDocument *qml = QmlDocument::create("asset:///main.qml");
    qml->documentContext()->setContextProperty("coreUrl", QVariant(QString::fromLatin1(url)));
    qml->documentContext()->setContextProperty("nativeHook", hook);
    if (qml->hasErrors()) {
        foreach (const QDeclarativeError &e, qml->errors()) qWarning() << e.toString();
        return 1;
    }
    AbstractPane *pane = qml->createRootObject<AbstractPane>();
    if (!pane) return 1;
    hook->pane = pane;
    Application::instance()->setScene(pane);
    return Application::exec();
}
