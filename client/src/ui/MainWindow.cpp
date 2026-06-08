#include "ui/MainWindow.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/Session.h"
#include "net/ApiClient.h"
#include "ui/FoodListPage.h"
#include "ui/OnboardingPage.h"
#include "ui/ProfilePage.h"
#include "ui/Theme.h"

MainWindow::MainWindow(ApiClient *api, Session *session, QWidget *parent)
    : QMainWindow(parent), api_(api), session_(session)
{
    setWindowTitle("Foodi");
    resize(900, 640);

    // --- custom top bar ---
    auto *topBar = new QWidget(this);
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(56);

    auto *tile = new QLabel(topBar);
    tile->setPixmap(theme::appTile(20));
    auto *word = new QLabel("Foodi", topBar);
    word->setObjectName("brandWord");

    navGroup_ = new QWidget(topBar);
    navGroup_->setObjectName("navGroup");
    // Keep the pill at its natural height; otherwise the horizontal top-bar
    // layout stretches this plain QWidget to the full 56px bar height.
    navGroup_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    foodsBtn_ = new QPushButton("Foods", navGroup_);
    profileBtn_ = new QPushButton("Profile", navGroup_);
    for (auto *b : {foodsBtn_, profileBtn_}) {
        b->setObjectName("navBtn");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *navGrp = new QButtonGroup(this);
    navGrp->setExclusive(true);
    navGrp->addButton(foodsBtn_);
    navGrp->addButton(profileBtn_);
    auto *navLay = new QHBoxLayout(navGroup_);
    navLay->setContentsMargins(3, 3, 3, 3);
    navLay->setSpacing(2);
    navLay->addWidget(foodsBtn_);
    navLay->addWidget(profileBtn_);

    auto *logout = new QPushButton("Log out", topBar);
    logout->setProperty("kind", "ghost");
    logout->setCursor(Qt::PointingHandCursor);

    connect(foodsBtn_, &QPushButton::clicked, this, &MainWindow::showFoodList);
    connect(profileBtn_, &QPushButton::clicked, this, &MainWindow::showProfile);
    connect(logout, &QPushButton::clicked, this, [this]() {
        session_->forgetPersisted();  // also drop any "remember me" token
        session_->clear();
        emit loggedOut();
    });

    auto *barLay = new QHBoxLayout(topBar);
    barLay->setContentsMargins(16, 0, 16, 0);
    barLay->setSpacing(10);
    barLay->addWidget(tile);
    barLay->addWidget(word);
    barLay->addSpacing(6);
    barLay->addWidget(navGroup_, 0, Qt::AlignVCenter);
    barLay->addStretch();
    barLay->addWidget(logout);

    // --- page stack ---
    stack_ = new QStackedWidget(this);
    onboarding_ = new OnboardingPage(api_, session_, this);
    connect(onboarding_, &OnboardingPage::completed, this, &MainWindow::showFoodList);
    // Skip: enter the app this session without saving. profile_completed stays
    // false in the DB, so the next launch routes back here to set up the profile.
    connect(onboarding_, &OnboardingPage::skipped, this, &MainWindow::showFoodList);
    foodList_ = new FoodListPage(api_, session_, this);
    profile_ = new ProfilePage(api_, session_, this);
    stack_->addWidget(onboarding_);
    stack_->addWidget(foodList_);
    stack_->addWidget(profile_);

    auto *central = new QWidget(this);
    central->setObjectName("pageRoot");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(topBar);
    root->addWidget(stack_, 1);
    setCentralWidget(central);
}

void MainWindow::refresh()
{
    if (!session_->profileCompleted) {
        setNavVisible(false);  // gate first-run users through onboarding
        stack_->setCurrentWidget(onboarding_);
        onboarding_->load();
    } else {
        showFoodList();
    }
}

void MainWindow::showFoodList()
{
    setNavVisible(true);
    foodsBtn_->setChecked(true);
    stack_->setCurrentWidget(foodList_);
    foodList_->load();
}

void MainWindow::showProfile()
{
    setNavVisible(true);
    profileBtn_->setChecked(true);
    stack_->setCurrentWidget(profile_);
    profile_->load();
}

void MainWindow::setNavVisible(bool visible)
{
    navGroup_->setVisible(visible);
}
