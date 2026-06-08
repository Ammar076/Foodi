#pragma once
#include <QWidget>

class ApiClient;
struct Session;
class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QCheckBox;
class QShowEvent;

// First screen: sign in to an existing account or create a new one. On success it
// fills the shared Session and emits authenticated(); main() then shows MainWindow.
// Styled as the design's centered auth card with a segmented Sign in / Create
// account control.
class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    LoginWindow(ApiClient *api, Session *session, QWidget *parent = nullptr);

signals:
    void authenticated();

protected:
    // Each time the screen reappears (e.g. after logout) start from a clean,
    // empty Sign-in tab — don't leave the previous user's text (or password) in
    // the fields.
    void showEvent(QShowEvent *event) override;

private:
    void clearFields();  // empty every input across all three forms
    QWidget *buildSignInForm();
    QWidget *buildRegisterForm();
    QWidget *buildForgotForm();  // forms_ index 2: a two-step reset view
    QWidget *field(const QString &label, QLineEdit *&out, bool password = false,
                   const QString &hint = QString());
    void showTab(int index);  // 0 = sign in, 1 = create account
    void showForgot();        // switch to the reset view (hides the tabs)
    void doLogin();
    void doRegister();
    void doSendResetCode();   // reset step 1
    void doResetPassword();   // reset step 2
    void setBusy(bool busy);
    void showError(const QString &msg);  // red
    void showInfo(const QString &msg);   // neutral
    void setStatus(const QString &msg, const QString &tone);

    ApiClient *api_;
    Session *session_;

    QLabel *title_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QWidget *authTabs_ = nullptr;  // hidden while the reset view is showing
    QPushButton *tabSignIn_ = nullptr;
    QPushButton *tabRegister_ = nullptr;
    QStackedWidget *forms_ = nullptr;
    QLabel *status_ = nullptr;

    QLineEdit *loginId_ = nullptr;
    QLineEdit *loginPass_ = nullptr;
    QCheckBox *remember_ = nullptr;
    QLineEdit *regUser_ = nullptr;
    QLineEdit *regEmail_ = nullptr;
    QLineEdit *regPass_ = nullptr;
    QLineEdit *regConfirm_ = nullptr;

    // Reset-password view (forms_ index 2).
    QStackedWidget *forgotSteps_ = nullptr;  // 0 = request code, 1 = enter code
    QLabel *resetSentMsg_ = nullptr;
    QLineEdit *resetId_ = nullptr;
    QLineEdit *resetCode_ = nullptr;
    QLineEdit *resetNewPass_ = nullptr;
    QLineEdit *resetConfirm_ = nullptr;
};
