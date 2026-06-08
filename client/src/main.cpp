#include <QApplication>
#include <QJsonDocument>

#include "core/Session.h"
#include "net/ApiClient.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

// Foodi desktop client. Flow: LoginWindow -> (auth) -> MainWindow.
// Session/ApiClient live for the whole app and are shared with the windows.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Org + app name give QSettings a stable location (used by "Remember me").
    QCoreApplication::setOrganizationName("Foodi");
    QCoreApplication::setApplicationName("Foodi");
    theme::apply(app);  // Fusion base + light palette + foodi.qss + app icon

    Session session;
    ApiClient api(&session);

    LoginWindow login(&api, &session);
    MainWindow home(&api, &session);

    QObject::connect(&login, &LoginWindow::authenticated, [&]() {
        login.hide();
        home.refresh();
        home.showMaximized();
    });
    QObject::connect(&home, &MainWindow::loggedOut, [&]() {
        home.hide();
        login.show();
    });

    // "Remember me": if we have a saved token, validate it silently before
    // deciding which window to show — so a returning user skips the login screen.
    const QString saved = Session::persistedToken();
    if (saved.isEmpty()) {
        login.show();
    } else {
        session.token = saved;
        api.getJson("/api/me", [&](bool ok, const QJsonDocument &body, const QString &) {
            if (ok) {
                session.setFromMe(body.object());
                home.refresh();
                home.showMaximized();
            } else {
                session.clear();
                session.forgetPersisted();  // stale/invalid token — drop it
                login.show();
            }
        });
    }
    return app.exec();
}
