#include "ui/Card.h"

#include <QLabel>
#include <QVBoxLayout>

Card::Card(QWidget *parent) : QFrame(parent) { init(QString()); }

Card::Card(const QString &title, QWidget *parent) : QFrame(parent) { init(title); }

void Card::init(const QString &title)
{
    setObjectName("card");

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    if (!title.isEmpty()) {
        auto *header = new QLabel(title, this);
        header->setObjectName("cardHeader");
        outer->addWidget(header);
    }

    auto *bodyW = new QWidget(this);
    bodyW->setObjectName("cardBody");
    body_ = new QVBoxLayout(bodyW);
    body_->setContentsMargins(16, 16, 16, 16);
    body_->setSpacing(12);
    outer->addWidget(bodyW);
}
