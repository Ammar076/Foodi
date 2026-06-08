#pragma once
#include <QFrame>

class QVBoxLayout;

// A flat, lightly-elevated panel matching the design's .groupbox / .card:
// white surface, hairline border, 8px corners. With a title it gets the
// surface-2 header bar; without one it's a plain padded card. Styling lives in
// foodi.qss (#card, #cardHeader); this just builds the structure.
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(QWidget *parent = nullptr);                    // headerless
    explicit Card(const QString &title, QWidget *parent = nullptr);

    // The body layout — add your rows/widgets here.
    QVBoxLayout *body() const { return body_; }

private:
    void init(const QString &title);
    QVBoxLayout *body_ = nullptr;
};
