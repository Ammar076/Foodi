#pragma once
#include <QWidget>

class QTimer;

// A small indeterminate loading spinner: a faint ring with a rotating teal arc,
// drawn with QPainter (no GIF asset). Matches the design's loading state. It
// auto-animates while visible and stops when hidden to avoid wasting cycles.
class Spinner : public QWidget
{
    Q_OBJECT
public:
    explicit Spinner(int size = 30, QWidget *parent = nullptr);

    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    int size_;
    int angle_ = 0;
    QTimer *timer_;
};
