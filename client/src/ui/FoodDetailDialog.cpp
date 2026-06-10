#include "ui/FoodDetailDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

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

// An "eyebrow" caption (small, uppercase, muted).
QLabel *eyebrow(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    l->setObjectName("eyebrow");
    return l;
}
}  // namespace

FoodDetailDialog::FoodDetailDialog(const QJsonObject &food, QWidget *parent)
    : QDialog(parent), nam_(new QNetworkAccessManager(this))
{
    const QString name = food.value("name").toString();
    setObjectName("detailDialog");
    // Modal surface follows the active theme (white in light, dark in dark).
    setStyleSheet(QStringLiteral("QDialog#detailDialog { background: %1; }")
                      .arg(theme::color("surface").name()));
    setWindowTitle(name + " — Foodi");
    setMinimumWidth(600);

    const QString status = food.value("status").toString();
    const QString brand = food.value("brand").toString();
    const QString category = food.value("category").toString();
    const QString diet = food.value("diet").toString();
    const bool isRecipe = food.value("kind").toString() == "recipe";

    // --- left: product image ---
    image_ = new QLabel(this);
    image_->setObjectName("imgPlaceholder");
    image_->setFixedSize(180, 180);
    image_->setAlignment(Qt::AlignCenter);
    image_->setText("…");

    // --- right column header: eyebrow / title / meta + big badge ---
    QStringList eyebrowParts;
    if (!brand.isEmpty())
        eyebrowParts << brand;
    if (!category.isEmpty())
        eyebrowParts << category;
    auto *eb = eyebrow(eyebrowParts.join("  ·  "), this);

    auto *nameLbl = new QLabel(name, this);
    nameLbl->setObjectName("pageTitle");
    nameLbl->setWordWrap(true);

    auto *metaLbl = new QLabel(diet.isEmpty() ? QStringLiteral("") : diet, this);
    metaLbl->setObjectName("detailMeta");
    metaLbl->setVisible(!diet.isEmpty());

    auto *bigBadge = badge::make(status, true, this);

    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(4);
    headerText->addWidget(eb);
    headerText->addWidget(nameLbl);
    headerText->addWidget(metaLbl);
    auto *headerRow = new QHBoxLayout;
    headerRow->addLayout(headerText, 1);
    headerRow->addWidget(bigBadge, 0, Qt::AlignTop);

    // --- verdict panel (driven by the user's own result) ---
    const QJsonObject yours = food.value("your_allergens").toObject();
    const QString youContains = joinNames(yours.value("contains").toArray());
    const QString youMay = joinNames(yours.value("may_contain").toArray());

    auto *verdict = new QFrame(this);
    verdict->setObjectName("verdict");
    verdict->setProperty("state", badge::stateKey(status));
    auto *glyph = new QLabel(verdict);
    glyph->setObjectName("verdictGlyph");
    auto *vTitle = new QLabel(verdict);
    vTitle->setObjectName("verdictTitle");
    vTitle->setWordWrap(true);
    auto *vSub = new QLabel(verdict);
    vSub->setObjectName("verdictSub");
    vSub->setWordWrap(true);
    if (status == "UNSAFE") {
        glyph->setText("✕");
        vTitle->setText(QStringLiteral("Contains %1 — an allergen you avoid").arg(youContains));
        vSub->setText("This product is not safe for you. See the ingredients below.");
    } else if (status == "CAUTION") {
        glyph->setText("!");
        vTitle->setText(QStringLiteral("May contain %1").arg(youMay));
        vSub->setText("Traces of an allergen you avoid may be present.");
    } else {
        glyph->setText("✓");
        vTitle->setText("Safe — none of your allergens detected");
        vSub->setText("Checked against your profile. Always read the pack to be sure.");
    }
    // Recipes carry no structured allergen tags, so their verdict is inferred from
    // the ingredient list — be honest that it's a best-effort check.
    if (isRecipe)
        vSub->setText(vSub->text() +
                      "  Allergens for recipes are estimated from the ingredient list.");
    auto *vText = new QVBoxLayout;
    vText->setSpacing(2);
    vText->addWidget(vTitle);
    vText->addWidget(vSub);
    auto *vLay = new QHBoxLayout(verdict);
    vLay->setContentsMargins(14, 12, 14, 12);
    vLay->setSpacing(11);
    vLay->addWidget(glyph, 0, Qt::AlignTop);
    vLay->addLayout(vText, 1);

    // --- ingredients ---
    const QString ingredients = food.value("ingredients_text").toString();
    auto *ingEyebrow = eyebrow("Ingredients", this);
    auto *ingLbl = new QLabel(
        ingredients.isEmpty() ? QStringLiteral("(not available)") : ingredients, this);
    ingLbl->setObjectName("ingredients");
    ingLbl->setWordWrap(true);

    // --- allergen check pills (full breakdown for everyone) ---
    auto *checkEyebrow = eyebrow("Allergen check", this);
    auto *pillsRow = new QHBoxLayout;
    pillsRow->setSpacing(8);
    for (const auto &v : yours.value("contains").toArray())
        pillsRow->addWidget(badge::pill("unsafe", "✕  " + v.toString(), this));
    for (const auto &v : yours.value("may_contain").toArray())
        pillsRow->addWidget(badge::pill("caution", "!  " + v.toString() + " (may contain)", this));
    const QString allContains = joinNames(food.value("contains").toArray());
    const QString allTraces = joinNames(food.value("traces").toArray());
    pillsRow->addWidget(badge::pill(
        "info", "Contains: " + (allContains.isEmpty() ? QStringLiteral("none") : allContains), this));
    if (!allTraces.isEmpty())
        pillsRow->addWidget(badge::pill("info", "Traces: " + allTraces, this));
    pillsRow->addStretch();

    // --- footer ---
    auto *source = new QLabel(
        isRecipe ? QStringLiteral("Source: Spoonacular")
                 : QStringLiteral("Source: Open Food Facts"),
        this);
    source->setObjectName("statusText");
    source->setProperty("tone", "neutral");
    auto *closeBtn = new QPushButton("Close", this);
    closeBtn->setProperty("kind", "primary");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto *footer = new QHBoxLayout;
    footer->addWidget(source);
    footer->addStretch();
    footer->addWidget(closeBtn);

    auto *rightCol = new QVBoxLayout;
    rightCol->setSpacing(0);
    rightCol->addLayout(headerRow);
    rightCol->addSpacing(16);
    rightCol->addWidget(verdict);
    rightCol->addSpacing(18);
    rightCol->addWidget(ingEyebrow);
    rightCol->addSpacing(6);
    rightCol->addWidget(ingLbl);
    rightCol->addSpacing(16);
    rightCol->addWidget(checkEyebrow);
    rightCol->addSpacing(8);
    rightCol->addLayout(pillsRow);
    rightCol->addStretch();

    auto *top = new QHBoxLayout;
    top->setSpacing(20);
    top->addWidget(image_, 0, Qt::AlignTop);
    top->addLayout(rightCol, 1);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color: %1;").arg(theme::color("border").name()));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(0);
    root->addLayout(top, 1);
    root->addSpacing(16);
    root->addWidget(line);
    root->addSpacing(14);
    root->addLayout(footer);

    // Fetch the product image off the OFF CDN (own NAM, so no auth header leaks).
    const QString url = food.value("image_url").toString();
    if (url.isEmpty()) {
        image_->setText("no image");
    } else {
        QNetworkReply *reply = nam_->get(QNetworkRequest(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            QPixmap pm;
            if (reply->error() == QNetworkReply::NoError && pm.loadFromData(reply->readAll())) {
                image_->setPixmap(
                    pm.scaled(image_->size() - QSize(8, 8), Qt::KeepAspectRatio,
                              Qt::SmoothTransformation));
            } else {
                image_->setText("no image");
            }
        });
    }
}
