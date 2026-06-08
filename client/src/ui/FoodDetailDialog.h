#pragma once
#include <QDialog>
#include <QJsonObject>

class QNetworkAccessManager;
class QLabel;

// Modal detail for one food: the per-user verdict, the full allergen breakdown,
// ingredients, and the product image (fetched lazily from the Open Food Facts URL).
// Styled as the design's detail dialog (verdict panel + allergen-check pills).
class FoodDetailDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FoodDetailDialog(const QJsonObject &food, QWidget *parent = nullptr);

private:
    QNetworkAccessManager *nam_;
    QLabel *image_ = nullptr;
};
