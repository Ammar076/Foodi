#include "ui/ProfilePage.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QVBoxLayout>

#include "core/Session.h"
#include "net/ApiClient.h"
#include "ui/AllergenPicker.h"
#include "ui/Card.h"
#include "ui/Theme.h"

namespace {
// A label-above-input field group; returns the container and hands back the edit.
QWidget *field(const QString &label, QLineEdit *&out, bool password = false)
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
    return w;
}

QPushButton *primaryBtn(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setProperty("kind", "primary");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
}  // namespace

ProfilePage::ProfilePage(ApiClient *api, Session *session, QWidget *parent)
    : QWidget(parent), api_(api), session_(session)
{
    // page heading
    auto *title = new QLabel("Profile");
    title->setObjectName("pageTitle");
    auto *sub = new QLabel("Manage your account and what we watch for.");
    sub->setObjectName("pageSub");

    // --- Account card (username editable, email read-only) ---
    auto *accCard = new Card("Account");
    auto *accGrid = new QGridLayout;
    accGrid->setHorizontalSpacing(14);
    accGrid->addWidget(field("Username", username_), 0, 0);
    accGrid->addWidget(field("Email", email_), 0, 1);
    email_->setReadOnly(true);
    auto *saveName = primaryBtn("Save name", accCard);
    connect(saveName, &QPushButton::clicked, this, &ProfilePage::saveUsername);
    auto *accBtnRow = new QHBoxLayout;
    accBtnRow->addStretch();
    accBtnRow->addWidget(saveName);
    accCard->body()->addLayout(accGrid);
    accCard->body()->addLayout(accBtnRow);

    // --- Change password card ---
    auto *pwCard = new Card("Change password");
    auto *pwGrid = new QGridLayout;
    pwGrid->setHorizontalSpacing(14);
    pwGrid->addWidget(field("Current password", curPass_, true), 0, 0);
    pwGrid->addWidget(field("New password", newPass_, true), 0, 1);
    newPass_->setPlaceholderText("at least 8 characters");
    auto *changePw = primaryBtn("Change password", pwCard);
    connect(changePw, &QPushButton::clicked, this, &ProfilePage::changePassword);
    auto *pwBtnRow = new QHBoxLayout;
    pwBtnRow->addStretch();
    pwBtnRow->addWidget(changePw);
    pwCard->body()->addLayout(pwGrid);
    pwCard->body()->addLayout(pwBtnRow);

    // --- Allergens card ---
    auto *alCard = new Card("Allergens to avoid");
    auto *hint = new QLabel("Foods containing these are flagged Unsafe; traces are flagged Caution.");
    hint->setObjectName("fieldHint");
    hint->setWordWrap(true);
    picker_ = new AllergenPicker(alCard);
    auto *saveAl = primaryBtn("Update allergens", alCard);
    connect(saveAl, &QPushButton::clicked, this, &ProfilePage::saveAllergens);
    auto *alBtnRow = new QHBoxLayout;
    alBtnRow->addStretch();
    alBtnRow->addWidget(saveAl);
    alCard->body()->addWidget(hint);
    alCard->body()->addWidget(picker_);
    alCard->body()->addLayout(alBtnRow);

    // --- Appearance card (theme) ---
    auto *appCard = new Card("Appearance");
    auto *appHint = new QLabel("Choose how Foodi looks. Default follows your system setting.");
    appHint->setObjectName("fieldHint");
    appHint->setWordWrap(true);
    auto *themeGroup = new QFrame(appCard);
    themeGroup->setObjectName("navGroup");
    themeGroup->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *themeLay = new QHBoxLayout(themeGroup);
    themeLay->setContentsMargins(3, 3, 3, 3);
    themeLay->setSpacing(2);
    struct ThemeOpt { const char *label; theme::Mode mode; };
    const ThemeOpt themeOpts[] = {{"Default", theme::Mode::System},
                                  {"Light", theme::Mode::Light},
                                  {"Dark", theme::Mode::Dark}};
    const theme::Mode curMode = theme::savedMode();
    for (const auto &opt : themeOpts) {
        auto *b = new QPushButton(opt.label, themeGroup);
        b->setObjectName("navBtn");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setChecked(opt.mode == curMode);
        const theme::Mode m = opt.mode;
        connect(b, &QPushButton::clicked, this, [m]() { theme::setMode(m); });
        themeLay->addWidget(b);
    }
    appCard->body()->addWidget(appHint);
    appCard->body()->addWidget(themeGroup, 0, Qt::AlignLeft);

    status_ = new QLabel(this);
    status_->setObjectName("statusText");
    status_->setProperty("tone", "neutral");
    status_->setWordWrap(true);

    // centered, scrollable column
    auto *column = new QWidget;
    column->setMaximumWidth(640);
    auto *colLay = new QVBoxLayout(column);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(16);
    colLay->addWidget(title);
    colLay->addWidget(sub);
    colLay->addWidget(accCard);
    colLay->addWidget(pwCard);
    colLay->addWidget(alCard);
    colLay->addWidget(appCard);
    colLay->addWidget(status_);
    colLay->addStretch();

    auto *centerRow = new QWidget;
    auto *centerLay = new QHBoxLayout(centerRow);
    centerLay->setContentsMargins(24, 24, 24, 24);
    centerLay->addStretch();
    centerLay->addWidget(column);
    centerLay->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(centerRow);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);
}

void ProfilePage::load()
{
    showStatus("");
    api_->getJson("/api/allergens", [this](bool ok, const QJsonDocument &body, const QString &err) {
        if (!ok) {
            showStatus(err, true);
            return;
        }
        const QJsonArray all = body.array();
        api_->getJson("/api/me", [this, all](bool ok2, const QJsonDocument &me, const QString &err2) {
            if (!ok2) {
                showStatus(err2, true);
                return;
            }
            const QJsonObject o = me.object();
            username_->setText(o.value("username").toString());
            email_->setText(o.value("email").toString());
            QSet<int> selected;
            for (const auto &v : o.value("allergens").toArray())
                selected.insert(v.toObject().value("id").toInt());
            picker_->setAllergens(all, selected);
        });
    });
}

void ProfilePage::saveUsername()
{
    const QString name = username_->text().trimmed();
    if (name.isEmpty()) {
        showStatus("Username can't be empty.", true);
        return;
    }
    const QJsonObject body{{"username", name}};
    api_->putJson("/api/me", body, [this](bool ok, const QJsonDocument &resp, const QString &err) {
        if (!ok) {
            showStatus(err, true);
            return;
        }
        session_->username = resp.object().value("username").toString();
        showStatus("Name updated.");
    });
}

void ProfilePage::changePassword()
{
    const QString cur = curPass_->text();
    const QString next = newPass_->text();
    if (next.size() < 8) {
        showStatus("New password must be at least 8 characters.", true);
        return;
    }
    const QJsonObject body{{"current_password", cur}, {"new_password", next}};
    api_->putJson("/api/me/password", body,
                  [this](bool ok, const QJsonDocument &, const QString &err) {
                      if (!ok) {
                          showStatus(err, true);
                          return;
                      }
                      curPass_->clear();
                      newPass_->clear();
                      showStatus("Password updated.");
                  });
}

void ProfilePage::saveAllergens()
{
    QJsonArray ids;
    for (int id : picker_->selectedIds())
        ids.append(id);
    const QJsonObject body{{"allergen_ids", ids}};
    api_->putJson("/api/me/allergens", body,
                  [this](bool ok, const QJsonDocument &, const QString &err) {
                      if (!ok) {
                          showStatus(err, true);
                          return;
                      }
                      showStatus("Allergens updated.");
                  });
}

void ProfilePage::showStatus(const QString &msg, bool error)
{
    status_->setProperty("tone", error ? "error" : "success");
    status_->setText(msg);
    // re-evaluate the property selector after changing it
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}
