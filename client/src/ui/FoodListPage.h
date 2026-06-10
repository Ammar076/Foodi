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
class QNetworkAccessManager;
class Spinner;

// The main browsing screen: a source toggle (All / Groceries / Recipes), a search
// box, category/diet filters and a "safe only" toggle, over a results table that
// shows each food's thumbnail and per-user safety badge. The results card swaps
// between three states: results, loading (spinner), and empty. A "Load more" button
// pages deeper without leaving the screen. Double-clicking (or Enter on) a row opens
// the detail dialog.
class FoodListPage : public QWidget
{
    Q_OBJECT
public:
    FoodListPage(ApiClient *api, Session *session, QWidget *parent = nullptr);

    // Populate the filter combos and run an initial (empty) search.
    void load();

private:
    void loadFilters();
    void runSearch();              // fresh search: resets to page 1, replaces the list
    void loadMore();               // appends the next page to the current list
    void fetchPage(bool append);   // shared request path
    void populate(const class QJsonArray &items, bool append);
    void loadThumb(int row, const QString &url);
    QString currentSource() const;  // "all" | "grocery" | "recipe"
    void openDetail(int foodId);
    void showResults();
    void showLoading();
    void showEmpty(const QString &title, const QString &message);

    ApiClient *api_;
    Session *session_;
    QNetworkAccessManager *imgNam_ = nullptr;  // thumbnail fetches (no auth header)

    QPushButton *srcAll_ = nullptr;
    QPushButton *srcGrocery_ = nullptr;
    QPushButton *srcRecipe_ = nullptr;

    QLineEdit *search_ = nullptr;
    QComboBox *category_ = nullptr;
    QComboBox *diet_ = nullptr;
    QCheckBox *safeOnly_ = nullptr;
    QPushButton *searchBtn_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *loadMoreBtn_ = nullptr;

    QStackedWidget *cardStack_ = nullptr;  // 0 results · 1 loading · 2 empty
    Spinner *spinner_ = nullptr;
    QLabel *emptyTitle_ = nullptr;
    QLabel *emptyMsg_ = nullptr;

    int page_ = 1;  // current page (1-based); runSearch resets, loadMore increments
};
