#include "ui/FoodListPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "net/ApiClient.h"
#include "ui/FoodDetailDialog.h"
#include "ui/Spinner.h"
#include "ui/StatusBadge.h"
#include "ui/Theme.h"

namespace {
QString joinNames(const QJsonArray &arr)
{
    QStringList l;
    for (const auto &v : arr)
        l << v.toString();
    return l.join(", ");
}

// Table tuned for the food list in two ways:
//
// 1) Column widths (resizeEvent): the Product column has priority — it grows to
//    fill wide windows, and when space runs out Category then Brand give way,
//    instead of Product collapsing to a couple of letters (what a Stretch column
//    does). Status/allergens stay fixed so the badge never clips.
//
// 2) Header + scrollbar geometry (updateGeometries): by default Qt runs the
//    vertical scrollbar the full height of the table — alongside the header row —
//    which makes it look like a row reaches up into the header. We instead stretch
//    the header across the full width (Status reaches the right edge) and drop the
//    scrollbar so it starts *below* the header, beside the rows only.
class FoodTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QTableWidget::resizeEvent(e);
        layoutColumns();
    }

    void updateGeometries() override
    {
        QTableWidget::updateGeometries();
        auto *vb = verticalScrollBar();
        auto *hh = horizontalHeader();
        if (hh->isHidden() || !vb->isVisible())
            return;

        const int headerH = hh->height();
        // Header spans the full width (covers the scrollbar gutter); stretchLast-
        // Section then grows Status to the edge.
        const int fullW = viewport()->width() + vb->width();
        if (hh->width() != fullW)
            hh->setGeometry(hh->x(), hh->y(), fullW, headerH);
        // Scrollbar starts just below the header.
        const int top = hh->y() + headerH;
        if (vb->y() != top) {
            const int bottom = vb->geometry().bottom();
            if (bottom > top)
                vb->setGeometry(vb->x(), top, vb->width(), bottom - top + 1);
        }
    }

private:
    void layoutColumns()
    {
        const int total = viewport()->width();
        if (total <= 0)
            return;

        const int status = 130;     // Status fills the remainder via stretchLastSection
        const int allergens = 160;  // kept fixed
        int brand = 130;
        int category = 160;
        const int productMin = 200;

        int product = total - status - allergens - brand - category;
        if (product < productMin) {
            // Keep Product readable; take the deficit from Category, then Brand.
            int deficit = productMin - product;
            product = productMin;
            auto shrink = [&deficit](int &col, int floor) {
                const int take = qMin(deficit, col - floor);
                if (take > 0) {
                    col -= take;
                    deficit -= take;
                }
            };
            shrink(category, 70);
            shrink(brand, 60);
        }
        setColumnWidth(0, product);
        setColumnWidth(1, brand);
        setColumnWidth(2, category);
        setColumnWidth(3, allergens);
        // Column 4 (Status) is the stretch-last section.
    }
};
}  // namespace

FoodListPage::FoodListPage(ApiClient *api, Session *session, QWidget *parent)
    : QWidget(parent), api_(api), session_(session)
{
    // --- page heading ---
    auto *title = new QLabel("Foods", this);
    title->setObjectName("pageTitle");
    auto *sub = new QLabel("Search a product to see if it's safe for you.", this);
    sub->setObjectName("pageSub");

    // --- filter bar ---
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search foods, brands…");
    search_->setClearButtonEnabled(true);
    search_->addAction(theme::searchIcon(), QLineEdit::LeadingPosition);

    category_ = new QComboBox(this);
    category_->setMinimumWidth(150);
    diet_ = new QComboBox(this);
    diet_->setMinimumWidth(130);
    safeOnly_ = new QCheckBox("Safe only", this);
    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setProperty("kind", "primary");
    searchBtn_->setCursor(Qt::PointingHandCursor);

    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    filters->addWidget(search_, 1);
    filters->addWidget(category_);
    filters->addWidget(diet_);
    filters->addWidget(safeOnly_);
    filters->addWidget(searchBtn_);

    // --- results table ---
    table_ = new FoodTable(0, 5, this);
    table_->setHorizontalHeaderLabels(
        {"Product", "Brand", "Category", "Your allergens", "Status"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);  // long names/categories get a trailing "…"
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(42);
    table_->horizontalHeader()->setHighlightSections(false);
    // Widths are driven by FoodTable::resizeEvent (Product keeps priority); Interactive
    // mode makes the header honor those widths instead of auto-fitting to content.
    // The last section (Status) stretches to fill the full-width header (see
    // FoodTable::updateGeometries) so it reaches the right edge past the scrollbar.
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    // Columns always fit (Product flexes, the rest elide), so horizontal scrolling
    // is never needed. Turning it off also avoids a phantom scrollbar from the
    // full-width header making the Status section a few px wider than the viewport.
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setFrameShape(QFrame::NoFrame);

    status_ = new QLabel(this);
    status_->setObjectName("statusBar");

    auto *resultsPage = new QWidget;
    auto *resLay = new QVBoxLayout(resultsPage);
    resLay->setContentsMargins(0, 0, 0, 0);
    resLay->setSpacing(0);
    resLay->addWidget(table_, 1);
    resLay->addWidget(status_);

    // --- loading page (spinner) ---
    auto *loadingPage = new QWidget;
    spinner_ = new Spinner(30, loadingPage);
    auto *loadTitle = new QLabel("Searching…", loadingPage);
    loadTitle->setObjectName("stateTitle");
    loadTitle->setAlignment(Qt::AlignCenter);
    auto *loadSub = new QLabel("Checking products against your allergens.", loadingPage);
    loadSub->setObjectName("pageSub");
    loadSub->setAlignment(Qt::AlignCenter);
    auto *loadLay = new QVBoxLayout(loadingPage);
    loadLay->addStretch();
    loadLay->addWidget(spinner_, 0, Qt::AlignHCenter);
    loadLay->addSpacing(14);
    loadLay->addWidget(loadTitle);
    loadLay->addWidget(loadSub);
    loadLay->addStretch();

    // --- empty page ---
    auto *emptyPage = new QWidget;
    auto *emptyIcon = new QLabel(emptyPage);
    emptyIcon->setPixmap(theme::emptyGlyph(36));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyTitle_ = new QLabel("No foods found", emptyPage);
    emptyTitle_->setObjectName("stateTitle");
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyMsg_ = new QLabel(emptyPage);
    emptyMsg_->setObjectName("pageSub");
    emptyMsg_->setAlignment(Qt::AlignHCenter);
    emptyMsg_->setWordWrap(true);
    // Fixed (not maximum) width so the word-wrapped height is computed correctly;
    // with only a max width the layout under-sizes the height and lines overlap.
    emptyMsg_->setFixedWidth(340);
    auto *clearBtn = new QPushButton("Clear search", emptyPage);
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        search_->clear();
        runSearch();
    });
    auto *emptyLay = new QVBoxLayout(emptyPage);
    emptyLay->addStretch();
    emptyLay->addWidget(emptyIcon, 0, Qt::AlignHCenter);
    emptyLay->addSpacing(14);
    emptyLay->addWidget(emptyTitle_);
    emptyLay->addWidget(emptyMsg_, 0, Qt::AlignHCenter);
    emptyLay->addSpacing(14);
    emptyLay->addWidget(clearBtn, 0, Qt::AlignHCenter);
    emptyLay->addStretch();

    cardStack_ = new QStackedWidget;
    cardStack_->addWidget(resultsPage);  // 0
    cardStack_->addWidget(loadingPage);  // 1
    cardStack_->addWidget(emptyPage);    // 2

    auto *card = new QFrame(this);
    card->setObjectName("card");
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->addWidget(cardStack_);

    // Search on button click / Enter; filter changes re-search automatically.
    // QComboBox::activated only fires on user action, so it won't storm during load().
    connect(searchBtn_, &QPushButton::clicked, this, &FoodListPage::runSearch);
    connect(search_, &QLineEdit::returnPressed, this, &FoodListPage::runSearch);
    connect(category_, &QComboBox::activated, this, [this](int) { runSearch(); });
    connect(diet_, &QComboBox::activated, this, [this](int) { runSearch(); });
    connect(safeOnly_, &QCheckBox::toggled, this, [this](bool) { runSearch(); });
    connect(table_, &QTableWidget::cellActivated, this, [this](int row, int) {
        if (auto *it = table_->item(row, 0))
            openDetail(it->data(Qt::UserRole).toInt());
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);
    root->addWidget(title);
    root->addWidget(sub);
    root->addLayout(filters);
    root->addWidget(card, 1);
}

void FoodListPage::load()
{
    loadFilters();
    runSearch();
}

void FoodListPage::loadFilters()
{
    api_->getJson("/api/foods/filters", [this](bool ok, const QJsonDocument &body, const QString &) {
        category_->clear();
        diet_->clear();
        category_->addItem("All categories", "");
        diet_->addItem("All diets", "");
        if (!ok)
            return;
        const QJsonObject o = body.object();
        for (const auto &c : o.value("categories").toArray())
            category_->addItem(c.toString(), c.toString());
        for (const auto &d : o.value("diets").toArray())
            diet_->addItem(d.toString(), d.toString());
    });
}

void FoodListPage::runSearch()
{
    QUrlQuery query;
    const QString text = search_->text().trimmed();
    if (!text.isEmpty())
        query.addQueryItem("q", text);
    const QString cat = category_->currentData().toString();
    if (!cat.isEmpty())
        query.addQueryItem("category", cat);
    const QString diet = diet_->currentData().toString();
    if (!diet.isEmpty())
        query.addQueryItem("diet", diet);
    if (safeOnly_->isChecked())
        query.addQueryItem("safe_only", "true");

    QString path = "/api/foods";
    if (!query.isEmpty())
        path += "?" + query.toString(QUrl::FullyEncoded);

    showLoading();
    searchBtn_->setEnabled(false);
    api_->getJson(path, [this, text](bool ok, const QJsonDocument &body, const QString &err) {
        searchBtn_->setEnabled(true);
        if (!ok) {
            showEmpty("Couldn't load foods", err);
            return;
        }
        const QJsonObject o = body.object();
        const QJsonArray items = o.value("items").toArray();
        if (items.isEmpty()) {
            showEmpty("No foods found",
                      text.isEmpty()
                          ? QStringLiteral("Try searching for a product or brand.")
                          : QStringLiteral("We couldn't match \"%1\". Try a product or brand name.")
                                .arg(text));
            return;
        }
        populate(items);
        status_->setText(QStringLiteral("%1 result(s)  ·  source: %2")
                             .arg(o.value("count").toInt())
                             .arg(o.value("source").toString()));
        showResults();
    });
}

void FoodListPage::populate(const QJsonArray &items)
{
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(items.size()));
    int r = 0;
    for (const auto &v : items) {
        const QJsonObject o = v.toObject();
        const QString status = o.value("status").toString();

        auto *name = new QTableWidgetItem(o.value("name").toString());
        name->setData(Qt::UserRole, o.value("id").toInt());  // food id rides on the row
        table_->setItem(r, 0, name);
        table_->setItem(r, 1, new QTableWidgetItem(o.value("brand").toString()));
        table_->setItem(r, 2, new QTableWidgetItem(o.value("category").toString()));

        const QString contains = joinNames(o.value("contains").toArray());
        const QString may = joinNames(o.value("may_contain").toArray());
        QStringList parts;
        if (!contains.isEmpty())
            parts << contains;
        if (!may.isEmpty())
            parts << QStringLiteral("(may: %1)").arg(may);
        auto *hit = new QTableWidgetItem(parts.join("  "));
        if (!contains.isEmpty())
            hit->setForeground(QColor("#a5301f"));
        else if (!may.isEmpty())
            hit->setForeground(QColor("#a65f0f"));
        else
            hit->setForeground(QColor("#9a948b"));
        table_->setItem(r, 3, hit);

        // Status badge as a (mouse-transparent) pill so row clicks still register.
        auto *cell = new QWidget;
        cell->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *cellLay = new QHBoxLayout(cell);
        cellLay->setContentsMargins(10, 4, 10, 4);
        cellLay->addWidget(badge::make(status), 0, Qt::AlignLeft | Qt::AlignVCenter);
        table_->setCellWidget(r, 4, cell);
        ++r;
    }
}

void FoodListPage::showResults() { cardStack_->setCurrentIndex(0); }

void FoodListPage::showLoading() { cardStack_->setCurrentIndex(1); }  // spinner auto-starts on show

void FoodListPage::showEmpty(const QString &title, const QString &message)
{
    emptyTitle_->setText(title);
    emptyMsg_->setText(message);
    cardStack_->setCurrentIndex(2);
}

void FoodListPage::openDetail(int foodId)
{
    api_->getJson(QStringLiteral("/api/foods/%1").arg(foodId),
                  [this](bool ok, const QJsonDocument &body, const QString &err) {
                      if (!ok) {
                          showEmpty("Couldn't open product", err);
                          return;
                      }
                      FoodDetailDialog dlg(body.object(), this);
                      dlg.exec();
                  });
}
