#include "ui/AllergenPicker.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayoutItem>
#include <QSet>

AllergenPicker::AllergenPicker(QWidget *parent) : QWidget(parent)
{
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(14);
    grid_->setVerticalSpacing(8);
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, 1);
}

void AllergenPicker::setAllergens(const QJsonArray &all, const QSet<int> &selected)
{
    boxes_.clear();
    QLayoutItem *item;
    while ((item = grid_->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    int i = 0;
    for (const auto &v : all) {
        const QJsonObject o = v.toObject();
        const int id = o.value("id").toInt();
        auto *cb = new QCheckBox(o.value("display_name").toString());
        cb->setProperty("allergenId", id);
        cb->setChecked(selected.contains(id));
        connect(cb, &QCheckBox::toggled, this, &AllergenPicker::selectionChanged);
        grid_->addWidget(cb, i / 2, i % 2);  // two columns; reparents cb
        boxes_.append(cb);
        ++i;
    }
}

QList<int> AllergenPicker::selectedIds() const
{
    QList<int> ids;
    for (auto *b : boxes_)
        if (b->isChecked())
            ids.append(b->property("allergenId").toInt());
    return ids;
}
