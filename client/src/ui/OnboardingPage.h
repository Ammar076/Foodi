#pragma once
#include <QWidget>

class ApiClient;
struct Session;
class AllergenPicker;
class QLabel;
class QPushButton;

// "Set up your profile" page: pick your allergens and save (which also flips
// profile_completed). Wraps the shared AllergenPicker with onboarding wording and
// a "Save & continue" action that advances to the food list.
class OnboardingPage : public QWidget
{
    Q_OBJECT
public:
    OnboardingPage(ApiClient *api, Session *session, QWidget *parent = nullptr);

    // Fetch the allergen list + current selection and (re)build the checklist.
    void load();

signals:
    void completed();
    void skipped();  // entered the app without saving; re-prompt next launch

private:
    void save();
    void setBusy(bool busy);
    void showStatus(const QString &msg, bool error = false);

    ApiClient *api_;
    Session *session_;

    AllergenPicker *picker_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *saveBtn_ = nullptr;
    QPushButton *skipBtn_ = nullptr;
};
