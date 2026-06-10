#include "ui/Spinner.h"

#include <QPainter>
#include <QPen>
#include <QTimer>

#include "ui/Theme.h"

Spinner::Spinner(int size, QWidget *parent) : QWidget(parent), size_(size)
{
    setFixedSize(size_, size_);
    timer_ = new QTimer(this);
    timer_->setInterval(28);  // ~36 fps
    connect(timer_, &QTimer::timeout, this, [this]() {
        angle_ = (angle_ + 12) % 360;
        update();
    });
}

void Spinner::start()
{
    if (!timer_->isActive())
        timer_->start();
}

void Spinner::stop() { timer_->stop(); }

void Spinner::showEvent(QShowEvent *) { start(); }
void Spinner::hideEvent(QHideEvent *) { stop(); }

void Spinner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal w = size_ * 0.12;
    const QRectF box(w, w, size_ - 2 * w, size_ - 2 * w);

    // faint full ring
    QPen base(theme::color("border"), w);
    base.setCapStyle(Qt::RoundCap);
    p.setPen(base);
    p.drawArc(box, 0, 360 * 16);

    // rotating teal arc (~270°)
    QPen arc(theme::color("brand"), w);
    arc.setCapStyle(Qt::RoundCap);
    p.setPen(arc);
    p.drawArc(box, -angle_ * 16, -270 * 16);
}
