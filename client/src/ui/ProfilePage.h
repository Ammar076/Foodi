#pragma once
#include <QWidget>

class ApiClient;
struct Session;
class AllergenPicker;
class QLineEdit;
class QLabel;

// Account settings: change username, change password, and edit allergens (reusing
// the shared AllergenPicker). Each section saves independently to its endpoint.
class ProfilePage : public QWidget
{
    Q_OBJECT
public:
    ProfilePage(ApiClient *api, Session *session, QWidget *parent = nullptr);

    // Refresh fields + allergen selection from the server. Call when shown.
    void load();

private:
    void saveUsername();
    void changePassword();
    void saveAllergens();
    void showStatus(const QString &msg, bool error = false);

    ApiClient *api_;
    Session *session_;

    QLineEdit *username_ = nullptr;
    QLineEdit *email_ = nullptr;
    QLineEdit *curPass_ = nullptr;
    QLineEdit *newPass_ = nullptr;
    AllergenPicker *picker_ = nullptr;
    QLabel *status_ = nullptr;
};
