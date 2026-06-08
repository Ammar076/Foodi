#pragma once
#include <QMainWindow>

class ApiClient;
struct Session;
class OnboardingPage;
class FoodListPage;
class ProfilePage;
class QStackedWidget;
class QWidget;
class QPushButton;

// Post-login shell. A custom top bar (brandmark · Foods | Profile · Log out) over
// a stack of pages. The nav is hidden during first-run onboarding. refresh()
// routes to the right page for the user's state after login.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ApiClient *api, Session *session, QWidget *parent = nullptr);

    void refresh();

signals:
    void loggedOut();

private:
    void showFoodList();
    void showProfile();
    void setNavVisible(bool visible);

    ApiClient *api_;
    Session *session_;

    QStackedWidget *stack_ = nullptr;
    OnboardingPage *onboarding_ = nullptr;
    FoodListPage *foodList_ = nullptr;
    ProfilePage *profile_ = nullptr;

    QWidget *navGroup_ = nullptr;
    QPushButton *foodsBtn_ = nullptr;
    QPushButton *profileBtn_ = nullptr;
};
