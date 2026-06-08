#include "ui/OnboardingPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QVBoxLayout>

#include "core/Session.h"
#include "net/ApiClient.h"
#include "ui/AllergenPicker.h"

OnboardingPage::OnboardingPage(ApiClient *api, Session *session, QWidget *parent)
    : QWidget(parent), api_(api), session_(session)
{
    auto *eyebrow = new QLabel("Set up your profile");
    eyebrow->setObjectName("eyebrow");
    auto *title = new QLabel("What should we watch for?");
    title->setObjectName("pageTitle");
    auto *sub = new QLabel(
        "Select the allergens you need to avoid. We'll flag any food that contains them.");
    sub->setObjectName("pageSub");
    sub->setWordWrap(true);

    // warm panel wrapping the allergen grid
    auto *panel = new QFrame;
    panel->setObjectName("softPanel");
    picker_ = new AllergenPicker(panel);
    auto *panelLay = new QVBoxLayout(panel);
    panelLay->setContentsMargins(18, 18, 18, 18);
    panelLay->addWidget(picker_);

    status_ = new QLabel;
    status_->setObjectName("statusText");
    status_->setProperty("tone", "neutral");

    skipBtn_ = new QPushButton("Skip for now");
    skipBtn_->setCursor(Qt::PointingHandCursor);
    connect(skipBtn_, &QPushButton::clicked, this, &OnboardingPage::skipped);

    saveBtn_ = new QPushButton("Save && continue");  // && -> literal "&" (mnemonic escape)
    saveBtn_->setProperty("kind", "primary");
    saveBtn_->setCursor(Qt::PointingHandCursor);
    connect(saveBtn_, &QPushButton::clicked, this, &OnboardingPage::save);
    connect(picker_, &AllergenPicker::selectionChanged, this, [this]() {
        if (saveBtn_->isEnabled())  // not mid-load/save
            showStatus("");
    });

    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(10);
    bottom->addWidget(status_);
    bottom->addStretch();
    bottom->addWidget(skipBtn_);
    bottom->addWidget(saveBtn_);

    auto *column = new QWidget;
    column->setMaximumWidth(560);
    auto *colLay = new QVBoxLayout(column);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(0);
    colLay->addWidget(eyebrow);
    colLay->addSpacing(8);
    colLay->addWidget(title);
    colLay->addSpacing(4);
    colLay->addWidget(sub);
    colLay->addSpacing(22);
    colLay->addWidget(panel);
    colLay->addSpacing(18);
    colLay->addLayout(bottom);

    auto *centerRow = new QWidget;
    auto *centerLay = new QHBoxLayout(centerRow);
    centerLay->setContentsMargins(24, 24, 24, 24);
    centerLay->addStretch();
    centerLay->addWidget(column, 0, Qt::AlignVCenter);
    centerLay->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(centerRow);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);
}

void OnboardingPage::load()
{
    setBusy(true);
    showStatus("Loading allergens…");
    api_->getJson("/api/allergens", [this](bool ok, const QJsonDocument &body, const QString &err) {
        if (!ok) {
            setBusy(false);
            showStatus(err, true);
            return;
        }
        const QJsonArray all = body.array();
        api_->getJson("/api/me", [this, all](bool ok2, const QJsonDocument &me, const QString &err2) {
            setBusy(false);
            if (!ok2) {
                showStatus(err2, true);
                return;
            }
            QSet<int> selected;
            for (const auto &v : me.object().value("allergens").toArray())
                selected.insert(v.toObject().value("id").toInt());
            picker_->setAllergens(all, selected);
            showStatus("");
        });
    });
}

void OnboardingPage::save()
{
    QJsonArray ids;
    for (int id : picker_->selectedIds())
        ids.append(id);

    setBusy(true);
    showStatus("Saving…");
    const QJsonObject body{{"allergen_ids", ids}};
    api_->putJson("/api/me/allergens", body,
                  [this](bool ok, const QJsonDocument &, const QString &err) {
                      setBusy(false);
                      if (!ok) {
                          showStatus(err, true);
                          return;
                      }
                      session_->profileCompleted = true;
                      emit completed();
                  });
}

void OnboardingPage::setBusy(bool busy)
{
    saveBtn_->setEnabled(!busy);
    skipBtn_->setEnabled(!busy);
    picker_->setEnabled(!busy);
}

void OnboardingPage::showStatus(const QString &msg, bool error)
{
    // Empty + idle → show the live selection count instead.
    QString text = msg;
    if (text.isEmpty() && !error)
        text = QStringLiteral("%1 selected · you can change these anytime")
                   .arg(picker_->selectedIds().size());
    status_->setProperty("tone", error ? "error" : "neutral");
    status_->setText(text);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}
