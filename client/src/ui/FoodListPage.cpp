#include "ui/FoodListPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
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

// Open Food Facts / recipe sources hand us names in inconsistent casing
// ("FLAMED BAKED PIZZA BASED", "classic margh pizza"). Title-case the ones that
// are entirely upper- or entirely lower-case; leave mixed-case names (e.g.
// "GF Pizza Base") untouched so we don't mangle real capitalization.
QString tidyName(const QString &s)
{
    bool hasLower = false, hasUpper = false;
    for (const QChar &c : s) {
        if (c.isLower())
            hasLower = true;
        else if (c.isUpper())
            hasUpper = true;
    }
    if (hasLower && hasUpper)
        return s;
    QStringList words = s.split(' ', Qt::SkipEmptyParts);
    for (QString &w : words) {
        w = w.toLower();
        if (!w.isEmpty())
            w[0] = w[0].toUpper();
    }
    return words.join(' ');
}

// The list renders thumbnails at ~38px, but the sources hand back much larger
// images (OFF 400px, Spoonacular 312px). Rewrite the URL to the smallest variant
// each CDN offers so downloads are a fraction of the size. Unknown hosts pass
// through unchanged. (The detail dialog still uses the full-size stored URL.)
QString thumbUrl(const QString &url)
{
    QString u = url;
    if (u.contains("openfoodfacts.org")) {
        static const QRegularExpression re(R"(\.(\d+|full)\.jpg$)",
                                           QRegularExpression::CaseInsensitiveOption);
        u.replace(re, ".100.jpg");
    } else if (u.contains("img.spoonacular.com")) {
        static const QRegularExpression re(R"(-\d+x\d+\.(jpg|png)$)",
                                           QRegularExpression::CaseInsensitiveOption);
        u.replace(re, "-90x90.\\1");
    }
    return u;
}

// Column indices for the results table.
enum Col { ColImage = 0, ColName, ColType, ColBrand, ColAllergens, ColStatus, ColCount };

// Table tuned for the food list in two ways:
//
// 1) Column widths (resizeEvent): the Product column has priority — it grows to
//    fill wide windows, and when space runs out Allergens, then Brand, then Type
//    give way, instead of Product collapsing. Image/Status stay fixed.
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
        const int fullW = viewport()->width() + vb->width();
        if (hh->width() != fullW)
            hh->setGeometry(hh->x(), hh->y(), fullW, headerH);
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

        const int image = 52;    // thumbnail
        const int status = 120;  // Status fills the remainder via stretchLastSection
        int type = 92;
        int brand = 120;

        // Product and Your-allergens share the leftover space. Allergens carries the
        // long EU-14 names ("Cereals containing gluten"), so it gets a real share
        // (~40%, capped) rather than a thin fixed strip — Product no longer eats it.
        int rest = total - image - status - type - brand;
        int allergens = qBound(180, rest * 40 / 100, 320);
        int product = rest - allergens;

        const int productMin = 200;
        if (product < productMin) {
            // Narrow window: protect Product, then claw width back for Allergens
            // from Brand and Type so it never collapses to a couple of letters.
            product = productMin;
            allergens = rest - product;
            auto reclaim = [&](int &col, int floor) {
                if (allergens >= 150)
                    return;
                const int take = qMin(150 - allergens, col - floor);
                if (take > 0) {
                    col -= take;
                    allergens += take;
                }
            };
            reclaim(brand, 70);
            reclaim(type, 70);
            if (allergens < 90)
                allergens = 90;
            product = total - image - status - type - brand - allergens;
            if (product < 130)
                product = 130;
        }
        setColumnWidth(ColImage, image);
        setColumnWidth(ColName, product);
        setColumnWidth(ColType, type);
        setColumnWidth(ColBrand, brand);
        setColumnWidth(ColAllergens, allergens);
        // Status is the stretch-last section.
    }
};
}  // namespace

FoodListPage::FoodListPage(ApiClient *api, Session *session, QWidget *parent)
    : QWidget(parent), api_(api), session_(session),
      imgNam_(new QNetworkAccessManager(this))
{
    // Persistent on-disk cache for thumbnails: repeat searches and paging back
    // don't re-hit the CDNs, and the cache survives app restarts.
    auto *cache = new QNetworkDiskCache(imgNam_);
    cache->setCacheDirectory(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs");
    cache->setMaximumCacheSize(64LL * 1024 * 1024);  // 64 MB
    imgNam_->setCache(cache);

    // --- page heading ---
    auto *title = new QLabel("Foods", this);
    title->setObjectName("pageTitle");
    auto *sub = new QLabel("Search groceries or recipes to see if they're safe for you.", this);
    sub->setObjectName("pageSub");

    // --- source toggle (All / Groceries / Recipes) ---
    auto *srcGroup = new QFrame(this);
    srcGroup->setObjectName("navGroup");
    auto *srcLay = new QHBoxLayout(srcGroup);
    srcLay->setContentsMargins(3, 3, 3, 3);
    srcLay->setSpacing(2);
    auto makeSeg = [&](const QString &text, bool checked) {
        auto *b = new QPushButton(text, srcGroup);
        b->setObjectName("navBtn");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(checked);
        b->setCursor(Qt::PointingHandCursor);
        srcLay->addWidget(b);
        connect(b, &QPushButton::clicked, this, &FoodListPage::runSearch);
        return b;
    };
    srcAll_ = makeSeg("All", true);
    srcGrocery_ = makeSeg("Groceries", false);
    srcRecipe_ = makeSeg("Recipes", false);

    auto *srcRow = new QHBoxLayout;
    srcRow->addWidget(srcGroup, 0, Qt::AlignLeft);
    srcRow->addStretch();

    // --- filter bar ---
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search foods, brands, dishes…");
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
    auto *t = new FoodTable(0, ColCount, this);
    table_ = t;
    table_->setHorizontalHeaderLabels(
        {"", "Product", "Type", "Brand", "Your allergens", "Status"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setIconSize(QSize(38, 38));
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(48);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setFrameShape(QFrame::NoFrame);

    loadMoreBtn_ = new QPushButton("Load more", this);
    loadMoreBtn_->setCursor(Qt::PointingHandCursor);
    connect(loadMoreBtn_, &QPushButton::clicked, this, &FoodListPage::loadMore);
    // Footer band: the button sits vertically centered inside it, so it floats with
    // breathing room instead of being pinned to the card's bottom edge. The whole
    // band collapses (hidden → no layout space) when there are no more pages.
    auto *moreBand = new QWidget;
    auto *moreLay = new QVBoxLayout(moreBand);
    moreLay->setContentsMargins(0, 0, 0, 0);
    moreLay->addStretch();
    moreLay->addWidget(loadMoreBtn_, 0, Qt::AlignHCenter);
    moreLay->addStretch();
    moreBand->setFixedHeight(60);
    moreBand->setVisible(false);

    auto *resultsPage = new QWidget;
    auto *resLay = new QVBoxLayout(resultsPage);
    resLay->setContentsMargins(0, 0, 0, 0);
    resLay->setSpacing(0);
    resLay->addWidget(table_, 1);
    resLay->addWidget(moreBand, 0);

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
    connect(searchBtn_, &QPushButton::clicked, this, &FoodListPage::runSearch);
    connect(search_, &QLineEdit::returnPressed, this, &FoodListPage::runSearch);
    connect(category_, &QComboBox::activated, this, [this](int) { runSearch(); });
    connect(diet_, &QComboBox::activated, this, [this](int) { runSearch(); });
    connect(safeOnly_, &QCheckBox::toggled, this, [this](bool) { runSearch(); });
    connect(table_, &QTableWidget::cellActivated, this, [this](int row, int) {
        if (auto *it = table_->item(row, ColImage))
            openDetail(it->data(Qt::UserRole).toInt());
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);
    root->addWidget(title);
    root->addWidget(sub);
    root->addLayout(srcRow);
    root->addLayout(filters);
    root->addWidget(card, 1);
}

QString FoodListPage::currentSource() const
{
    if (srcRecipe_->isChecked())
        return "recipe";
    if (srcGrocery_->isChecked())
        return "grocery";
    return "all";
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
    page_ = 1;
    fetchPage(false);
}

void FoodListPage::loadMore()
{
    ++page_;
    fetchPage(true);
}

void FoodListPage::fetchPage(bool append)
{
    QUrlQuery query;
    const QString text = search_->text().trimmed();
    if (!text.isEmpty())
        query.addQueryItem("q", text);
    query.addQueryItem("source", currentSource());
    const QString cat = category_->currentData().toString();
    if (!cat.isEmpty())
        query.addQueryItem("category", cat);
    const QString diet = diet_->currentData().toString();
    if (!diet.isEmpty())
        query.addQueryItem("diet", diet);
    if (safeOnly_->isChecked())
        query.addQueryItem("safe_only", "true");
    query.addQueryItem("page", QString::number(page_));

    QString path = "/api/foods?" + query.toString(QUrl::FullyEncoded);

    if (append) {
        loadMoreBtn_->setEnabled(false);
        loadMoreBtn_->setText("Loading…");
    } else {
        showLoading();
    }
    searchBtn_->setEnabled(false);

    api_->getJson(path, [this, text, append](bool ok, const QJsonDocument &body, const QString &err) {
        searchBtn_->setEnabled(true);
        loadMoreBtn_->setEnabled(true);
        loadMoreBtn_->setText("Load more");

        if (!ok) {
            if (append) {
                --page_;  // roll back the failed page so the next click retries it
                return;
            }
            showEmpty("Couldn't load foods", err);
            return;
        }
        const QJsonObject o = body.object();
        const QJsonArray items = o.value("items").toArray();
        if (!append && items.isEmpty()) {
            showEmpty("No foods found",
                      text.isEmpty()
                          ? QStringLiteral("Try searching for a product, brand, or dish.")
                          : QStringLiteral("We couldn't match \"%1\". Try another product or dish.")
                                .arg(text));
            return;
        }
        populate(items, append);
        loadMoreBtn_->parentWidget()->setVisible(o.value("has_more").toBool());
        if (!append)
            showResults();
    });
}

void FoodListPage::populate(const QJsonArray &items, bool append)
{
    int r = append ? table_->rowCount() : 0;
    if (!append)
        table_->setRowCount(0);
    table_->setRowCount(r + static_cast<int>(items.size()));

    for (const auto &v : items) {
        const QJsonObject o = v.toObject();
        const QString status = o.value("status").toString();
        const int id = o.value("id").toInt();
        const bool isRecipe = o.value("kind").toString() == "recipe";

        // Image cell (icon set async); the food id rides on this cell for row clicks.
        auto *img = new QTableWidgetItem();
        img->setData(Qt::UserRole, id);
        table_->setItem(r, ColImage, img);

        auto *name = new QTableWidgetItem(tidyName(o.value("name").toString()));
        table_->setItem(r, ColName, name);

        auto *type = new QTableWidgetItem(isRecipe ? QStringLiteral("Recipe")
                                                   : QStringLiteral("Grocery"));
        type->setForeground(isRecipe ? theme::color("brand") : theme::color("ink2"));
        table_->setItem(r, ColType, type);

        table_->setItem(r, ColBrand, new QTableWidgetItem(o.value("brand").toString()));

        const QString contains = joinNames(o.value("contains").toArray());
        const QString may = joinNames(o.value("may_contain").toArray());
        QStringList parts;
        if (!contains.isEmpty())
            parts << contains;
        if (!may.isEmpty())
            parts << QStringLiteral("(may: %1)").arg(may);
        auto *hit = new QTableWidgetItem(parts.join("  "));
        if (!contains.isEmpty())
            hit->setForeground(theme::color("unsafeInk"));
        else if (!may.isEmpty())
            hit->setForeground(theme::color("cautionInk"));
        else
            hit->setForeground(theme::color("ink3"));
        table_->setItem(r, ColAllergens, hit);

        // Status badge as a (mouse-transparent) pill so row clicks still register.
        auto *cell = new QWidget;
        cell->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *cellLay = new QHBoxLayout(cell);
        cellLay->setContentsMargins(10, 4, 10, 4);
        cellLay->addWidget(badge::make(status), 0, Qt::AlignLeft | Qt::AlignVCenter);
        table_->setCellWidget(r, ColStatus, cell);

        loadThumb(r, o.value("image_url").toString());
        ++r;
    }
}

void FoodListPage::loadThumb(int row, const QString &url)
{
    auto *item = table_->item(row, ColImage);
    if (!item)
        return;
    // Show the placeholder immediately; it stays for foods with no photo and is
    // replaced in-place once a real thumbnail arrives.
    item->setIcon(QIcon(theme::thumbPlaceholder(38)));
    if (url.isEmpty())
        return;

    const QString small = thumbUrl(url);

    // In-memory cache: a thumbnail we've already decoded this session goes straight
    // to the cell with no network or disk hit.
    QPixmap cached;
    if (QPixmapCache::find(small, &cached)) {
        item->setIcon(QIcon(cached));
        return;
    }

    // Capture the food id so a late reply that lands after the list was rebuilt (or
    // paged) is dropped instead of decorating the wrong row.
    const int expectId = item->data(Qt::UserRole).toInt();

    QNetworkRequest req((QUrl(small)));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = imgNam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, row, expectId, small]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll()))
            return;
        const QPixmap scaled = pm.scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmapCache::insert(small, scaled);
        auto *it = table_->item(row, ColImage);
        if (!it || it->data(Qt::UserRole).toInt() != expectId)
            return;  // row was reused for a different food; ignore
        it->setIcon(QIcon(scaled));
    });
}

void FoodListPage::showResults() { cardStack_->setCurrentIndex(0); }

void FoodListPage::showLoading() { cardStack_->setCurrentIndex(1); }  // spinner auto-starts on show

void FoodListPage::showEmpty(const QString &title, const QString &message)
{
    loadMoreBtn_->parentWidget()->setVisible(false);
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
