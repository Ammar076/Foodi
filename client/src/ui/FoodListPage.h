#pragma once
#include <QWidget>

class ApiClient;
struct Session;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QLabel;
class QStackedWidget;
class Spinner;

// The main browsing screen: a search box, category/diet filters and a "safe only"
// toggle, over a results table that shows each food's per-user safety badge. The
// results card swaps between three states: results, loading (spinner), and empty.
// Double-clicking (or Enter on) a row opens the detail dialog.
class FoodListPage : public QWidget
{
    Q_OBJECT
public:
    FoodListPage(ApiClient *api, Session *session, QWidget *parent = nullptr);

    // Populate the filter combos and run an initial (empty) search.
    void load();

private:
    void loadFilters();
    void runSearch();
    void populate(const class QJsonArray &items);
    void openDetail(int foodId);
    void showResults();
    void showLoading();
    void showEmpty(const QString &title, const QString &message);

    ApiClient *api_;
    Session *session_;

    QLineEdit *search_ = nullptr;
    QComboBox *category_ = nullptr;
    QComboBox *diet_ = nullptr;
    QCheckBox *safeOnly_ = nullptr;
    QPushButton *searchBtn_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *status_ = nullptr;  // footer line on the results page

    QStackedWidget *cardStack_ = nullptr;  // 0 results · 1 loading · 2 empty
    Spinner *spinner_ = nullptr;
    QLabel *emptyTitle_ = nullptr;
    QLabel *emptyMsg_ = nullptr;
};
