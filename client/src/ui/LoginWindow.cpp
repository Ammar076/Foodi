#include "ui/LoginWindow.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "core/Session.h"
#include "net/ApiClient.h"
#include "ui/Card.h"
#include "ui/Theme.h"

LoginWindow::LoginWindow(ApiClient *api, Session *session, QWidget *parent)
    : QWidget(parent), api_(api), session_(session)
{
    setObjectName("loginRoot");
    setWindowTitle("Foodi");
    resize(480, 660);

    // --- the centered auth card ---
    auto *card = new Card(this);
    card->setFixedWidth(380);
    card->body()->setContentsMargins(28, 28, 28, 28);
    card->body()->setSpacing(0);

    auto *tile = new QLabel(card);
    tile->setPixmap(theme::appTile(40));
    tile->setAlignment(Qt::AlignCenter);

    title_ = new QLabel("Welcome back", card);
    title_->setObjectName("authTitle");
    title_->setAlignment(Qt::AlignCenter);
    subtitle_ = new QLabel("Sign in to check your foods.", card);
    subtitle_->setObjectName("pageSub");
    subtitle_->setAlignment(Qt::AlignCenter);

    // segmented tabs
    authTabs_ = new QWidget(card);
    authTabs_->setObjectName("authTabs");
    tabSignIn_ = new QPushButton("Sign in", authTabs_);
    tabRegister_ = new QPushButton("Create account", authTabs_);
    for (auto *t : {tabSignIn_, tabRegister_})
    {
        t->setObjectName("authTab");
        t->setCheckable(true);
        t->setCursor(Qt::PointingHandCursor);
    }
    auto *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    grp->addButton(tabSignIn_, 0);
    grp->addButton(tabRegister_, 1);
    tabSignIn_->setChecked(true);
    auto *tabLay = new QHBoxLayout(authTabs_);
    tabLay->setContentsMargins(3, 3, 3, 3);
    tabLay->setSpacing(2);
    tabLay->addWidget(tabSignIn_);
    tabLay->addWidget(tabRegister_);

    forms_ = new QStackedWidget(card);
    forms_->addWidget(buildSignInForm());   // 0
    forms_->addWidget(buildRegisterForm()); // 1
    forms_->addWidget(buildForgotForm());   // 2

    status_ = new QLabel(card);
    status_->setObjectName("statusText");
    status_->setProperty("tone", "error");
    status_->setWordWrap(true);

    connect(tabSignIn_, &QPushButton::clicked, this, [this]()
            { showTab(0); });
    connect(tabRegister_, &QPushButton::clicked, this, [this]()
            { showTab(1); });

    auto *b = card->body();
    b->addWidget(tile);
    b->addSpacing(12);
    b->addWidget(title_);
    b->addWidget(subtitle_);
    b->addSpacing(20);
    b->addWidget(authTabs_);
    b->addSpacing(18);
    b->addWidget(forms_);
    b->addSpacing(10);
    b->addWidget(status_);

    // center the card on the page (full alignment keeps it at its natural size)
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
    auto *root = new QVBoxLayout(this);
    root->addStretch();
    root->addWidget(card, 0, Qt::AlignCenter);
    root->addStretch();
}

QWidget *LoginWindow::field(const QString &label, QLineEdit *&out, bool password,
                            const QString &hint)
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto *lbl = new QLabel(label, w);
    lbl->setObjectName("fieldLabel");
    out = new QLineEdit(w);
    if (password)
        out->setEchoMode(QLineEdit::Password);
    lay->addWidget(lbl);
    lay->addWidget(out);
    if (!hint.isEmpty())
    {
        auto *h = new QLabel(hint, w);
        h->setObjectName("fieldHint");
        lay->addWidget(h);
    }
    return w;
}

QWidget *LoginWindow::buildSignInForm()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);

    lay->addWidget(field("Email or username", loginId_));
    loginId_->setPlaceholderText("you@example.com or username");
    lay->addWidget(field("Password", loginPass_, true));
    loginPass_->setPlaceholderText("Your password");

    // Remember me · Forgot password?
    remember_ = new QCheckBox("Remember me", w);
    remember_->setChecked(true);
    remember_->setCursor(Qt::PointingHandCursor);
    auto *forgot = new QPushButton("Forgot password?", w);
    forgot->setObjectName("linkBtn");
    forgot->setFlat(true);
    forgot->setCursor(Qt::PointingHandCursor);
    connect(forgot, &QPushButton::clicked, this, &LoginWindow::showForgot);
    auto *optRow = new QHBoxLayout;
    optRow->setContentsMargins(0, 0, 0, 0);
    optRow->addWidget(remember_);
    optRow->addStretch();
    optRow->addWidget(forgot);
    lay->addLayout(optRow);

    auto *btn = new QPushButton("Sign in", w);
    btn->setProperty("kind", "primary");
    btn->setDefault(true);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, this, &LoginWindow::doLogin);
    connect(loginPass_, &QLineEdit::returnPressed, this, &LoginWindow::doLogin);
    lay->addWidget(btn);
    lay->addStretch(); // pool any extra height here so fields stay compact
    return w;
}

QWidget *LoginWindow::buildRegisterForm()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);

    lay->addWidget(field("Username", regUser_));
    regUser_->setPlaceholderText("username");
    lay->addWidget(field("Email", regEmail_));
    regEmail_->setPlaceholderText("you@example.com");
    lay->addWidget(field("Password", regPass_, true, "At least 8 characters."));
    lay->addWidget(field("Confirm password", regConfirm_, true));

    auto *btn = new QPushButton("Create account", w);
    btn->setProperty("kind", "primary");
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, this, &LoginWindow::doRegister);
    connect(regConfirm_, &QLineEdit::returnPressed, this, &LoginWindow::doRegister);
    lay->addWidget(btn);
    lay->addStretch(); // pool any extra height here so fields stay compact
    return w;
}

QWidget *LoginWindow::buildForgotForm()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);

    // Small helper for the "Back to sign in" link both steps share.
    auto backLink = [this](QWidget *p)
    {
        auto *back = new QPushButton("Back to sign in", p);
        back->setObjectName("linkBtn");
        back->setFlat(true);
        back->setCursor(Qt::PointingHandCursor);
        connect(back, &QPushButton::clicked, this, [this]()
                { showTab(0); });
        return back;
    };

    // --- step 0: request a code ---
    auto *stepRequest = new QWidget;
    auto *reqLay = new QVBoxLayout(stepRequest);
    reqLay->setContentsMargins(0, 0, 0, 0);
    reqLay->setSpacing(16);
    reqLay->addWidget(field("Email or username", resetId_));
    resetId_->setPlaceholderText("you@example.com or username");
    auto *sendBtn = new QPushButton("Send reset code", stepRequest);
    sendBtn->setProperty("kind", "primary");
    sendBtn->setCursor(Qt::PointingHandCursor);
    connect(sendBtn, &QPushButton::clicked, this, &LoginWindow::doSendResetCode);
    connect(resetId_, &QLineEdit::returnPressed, this, &LoginWindow::doSendResetCode);
    reqLay->addWidget(sendBtn);
    reqLay->addWidget(backLink(stepRequest), 0, Qt::AlignHCenter);
    // This step is shorter than the "enter code" step they share a stack with;
    // without a trailing stretch the field/button get spread to fill the height.
    reqLay->addStretch();

    // --- step 1: enter code + new password ---
    auto *stepReset = new QWidget;
    auto *resLay = new QVBoxLayout(stepReset);
    resLay->setContentsMargins(0, 0, 0, 0);
    resLay->setSpacing(16);
    resetSentMsg_ = new QLabel(stepReset);
    resetSentMsg_->setObjectName("fieldHint");
    resetSentMsg_->setWordWrap(true);
    resLay->addWidget(resetSentMsg_);
    resLay->addWidget(field("Reset code", resetCode_));
    resetCode_->setPlaceholderText("e.g. ABCD-EFGH");
    resLay->addWidget(field("New password", resetNewPass_, true, "At least 8 characters."));
    resLay->addWidget(field("Confirm new password", resetConfirm_, true));
    auto *resetBtn = new QPushButton("Reset password", stepReset);
    resetBtn->setProperty("kind", "primary");
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &LoginWindow::doResetPassword);
    connect(resetConfirm_, &QLineEdit::returnPressed, this, &LoginWindow::doResetPassword);
    resLay->addWidget(resetBtn);
    resLay->addWidget(backLink(stepReset), 0, Qt::AlignHCenter);

    forgotSteps_ = new QStackedWidget(w);
    forgotSteps_->addWidget(stepRequest); // 0
    forgotSteps_->addWidget(stepReset);   // 1
    lay->addWidget(forgotSteps_);
    lay->addStretch(); // pool extra height so fields stay compact
    return w;
}

void LoginWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    clearFields();
    showTab(0); // always reappear on a clean Sign-in tab
}

void LoginWindow::clearFields()
{
    for (QLineEdit *e : {loginId_, loginPass_, regUser_, regEmail_, regPass_, regConfirm_,
                         resetId_, resetCode_, resetNewPass_, resetConfirm_})
        e->clear();
    remember_->setChecked(true); // back to the default
}

void LoginWindow::showTab(int index)
{
    authTabs_->show(); // may have been hidden by the reset view
    tabSignIn_->setChecked(index == 0);
    tabRegister_->setChecked(index == 1);
    forms_->setCurrentIndex(index);
    title_->setText(index == 0 ? "Welcome back" : "Create your account");
    subtitle_->setText(index == 0 ? "Sign in to check your foods."
                                  : "Takes a minute. Set your allergens next.");
    status_->clear();
}

void LoginWindow::showForgot()
{
    forms_->setCurrentIndex(2);
    forgotSteps_->setCurrentIndex(0); // start at "request a code"
    authTabs_->hide();                // not one of the two tabs
    title_->setText("Reset your password");
    subtitle_->setText("We'll email you a code to set a new one.");
    status_->clear();
    resetId_->setText(loginId_->text().trimmed()); // carry over what they typed
    resetId_->setFocus();
}

void LoginWindow::doLogin()
{
    const QString id = loginId_->text().trimmed();
    const QString pass = loginPass_->text();
    if (id.isEmpty())
    {
        showError("Enter your email or username.");
        loginId_->setFocus();
        return;
    }
    if (pass.isEmpty())
    {
        showError("Enter your password.");
        loginPass_->setFocus();
        return;
    }
    setBusy(true);
    api_->login(id, pass, [this](bool ok, const QJsonDocument &body, const QString &err)
                {
        setBusy(false);
        if (!ok) {
            showError(err);
            loginPass_->setFocus();
            loginPass_->selectAll();
            return;
        }
        session_->setFromAuthResponse(body.object());
        if (remember_->isChecked())
            session_->persist();
        else
            session_->forgetPersisted();
        emit authenticated(); });
}

void LoginWindow::doRegister()
{
    const QString user = regUser_->text().trimmed();
    const QString email = regEmail_->text().trimmed();
    const QString pass = regPass_->text();
    const QString confirm = regConfirm_->text();
    if (user.isEmpty())
    {
        showError("Choose a username.");
        regUser_->setFocus();
        return;
    }
    if (email.isEmpty())
    {
        showError("Enter your email.");
        regEmail_->setFocus();
        return;
    }
    if (pass.size() < 8)
    {
        showError("Password must be at least 8 characters.");
        regPass_->setFocus();
        return;
    }
    if (pass != confirm)
    {
        showError("Passwords don't match.");
        regConfirm_->setFocus();
        regConfirm_->selectAll();
        return;
    }
    setBusy(true);
    api_->registerUser(
        user, email, pass, [this](bool ok, const QJsonDocument &body, const QString &err)
        {
            setBusy(false);
            if (!ok) {
                showError(err);
                // Point at the field the server most likely rejected.
                if (err.contains("email", Qt::CaseInsensitive))
                    regEmail_->setFocus();
                else
                    regUser_->setFocus();
                return;
            }
            session_->setFromAuthResponse(body.object());
            session_->persist();  // new accounts stay signed in
            emit authenticated(); });
}

void LoginWindow::doSendResetCode()
{
    const QString id = resetId_->text().trimmed();
    if (id.isEmpty())
    {
        showError("Enter your email or username.");
        resetId_->setFocus();
        return;
    }
    setBusy(true);
    api_->forgotPassword(id, [this](bool ok, const QJsonDocument &, const QString &err)
                         {
        setBusy(false);
        if (!ok) {
            showError(err);
            resetId_->setFocus();
            return;
        }
        // The server replies the same way whether or not the account exists (so it
        // can't be used to probe for accounts), so we always advance to step 2.
        forgotSteps_->setCurrentIndex(1);
        title_->setText("Enter your code");
        subtitle_->setText("Check your email for the reset code.");
        resetSentMsg_->setText(
            QStringLiteral("We've emailed you a reset code. "
                           "Enter it below with your new password."));
        showInfo("Reset code sent if the account exists.");
        resetCode_->setFocus(); });
}

void LoginWindow::doResetPassword()
{
    const QString id = resetId_->text().trimmed();
    const QString code = resetCode_->text().trimmed();
    const QString pass = resetNewPass_->text();
    const QString confirm = resetConfirm_->text();
    if (code.isEmpty())
    {
        showError("Enter the reset code from your email.");
        resetCode_->setFocus();
        return;
    }
    if (pass.size() < 8)
    {
        showError("Password must be at least 8 characters.");
        resetNewPass_->setFocus();
        return;
    }
    if (pass != confirm)
    {
        showError("Passwords don't match.");
        resetConfirm_->setFocus();
        resetConfirm_->selectAll();
        return;
    }
    setBusy(true);
    api_->resetPassword(id, code, pass,
                        [this, id](bool ok, const QJsonDocument &, const QString &err)
                        {
                            setBusy(false);
                            if (!ok)
                            {
                                showError(err);
                                resetCode_->setFocus();
                                resetCode_->selectAll();
                                return;
                            }
                            showTab(0); // back to sign in (also re-shows the tabs)
                            loginId_->setText(id);
                            loginPass_->clear();
                            loginPass_->setFocus();
                            showInfo("Password updated. Sign in with your new password.");
                        });
}

void LoginWindow::setBusy(bool busy)
{
    forms_->setEnabled(!busy);
    tabSignIn_->setEnabled(!busy);
    tabRegister_->setEnabled(!busy);
    if (busy)
        status_->clear();
}

void LoginWindow::showError(const QString &msg) { setStatus(msg, "error"); }

void LoginWindow::showInfo(const QString &msg) { setStatus(msg, "neutral"); }

void LoginWindow::setStatus(const QString &msg, const QString &tone)
{
    status_->setProperty("tone", tone);
    status_->setText(msg);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}
