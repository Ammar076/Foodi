#include "ui/Theme.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QProxyStyle>
#include <QStyleFactory>

namespace {

// Thin wrapper over the base style that forces combo-box dropdowns to open as a
// plain list *below* the box. Without this, the style opens the macOS-flavoured
// "popup" that aligns the currently-selected item over the box — so picking a
// item low in the list makes the next open shift up and spill off-screen.
class FoodiStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

}  // namespace

namespace theme {

void apply(QApplication &app)
{
    // Fusion is a fully style-sheet-friendly base; the native Windows style
    // ignores parts of QSS (checkbox indicators, combo frames), so we swap it
    // out and let foodi.qss define the whole look. The proxy adds the combo-popup
    // fix above (it takes ownership of the Fusion instance).
    app.setStyle(new FoodiStyle(QStyleFactory::create("Fusion")));

    // Force a light palette. The QSS paints the widgets we name, but anything we
    // don't (e.g. scroll-area viewports) falls back to the palette — and on a
    // dark-mode OS that fallback is dark (the "black background" bug). This makes
    // Foodi a light-theme app regardless of the OS theme.
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#f1efe9"));
    pal.setColor(QPalette::WindowText, QColor("#2c2926"));
    pal.setColor(QPalette::Base, QColor("#ffffff"));
    pal.setColor(QPalette::AlternateBase, QColor("#f8f6f1"));
    pal.setColor(QPalette::Text, QColor("#2c2926"));
    pal.setColor(QPalette::Button, QColor("#ffffff"));
    pal.setColor(QPalette::ButtonText, QColor("#2c2926"));
    pal.setColor(QPalette::PlaceholderText, QColor("#9a948b"));
    pal.setColor(QPalette::Highlight, QColor("#1a857a"));
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::ToolTipBase, QColor("#2c2926"));
    pal.setColor(QPalette::ToolTipText, QColor("#ffffff"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#9a948b"));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9a948b"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9a948b"));
    app.setPalette(pal);

    QFont f("Segoe UI");
    f.setPixelSize(13);
    app.setFont(f);

    QFile qss(":/foodi.qss");
    if (qss.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    app.setWindowIcon(appIcon());
}

QPixmap appTile(int px)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // rounded teal tile
    const qreal r = px * 0.28;
    QPainterPath tile;
    tile.addRoundedRect(QRectF(0, 0, px, px), r, r);
    p.fillPath(tile, QColor(kBrand));

    // white check, mapped from the design's 14-unit path 3,7.3 5.7,10 11,4.2
    const qreal L = px * 0.6;
    const qreal o = (px - L) / 2.0;
    const qreal s = L / 14.0;
    QPen pen(Qt::white, px * 0.13);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath check;
    check.moveTo(o + 3 * s, o + 7.3 * s);
    check.lineTo(o + 5.7 * s, o + 10 * s);
    check.lineTo(o + 11 * s, o + 4.2 * s);
    p.drawPath(check);
    p.end();
    return pm;
}

QIcon appIcon()
{
    QIcon icon;
    for (int sz : {16, 24, 32, 48, 64, 256})
        icon.addPixmap(appTile(sz));
    return icon;
}

QIcon searchIcon(int px)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(kInk3), px * 0.1);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 16-unit glyph: circle cx7 cy7 r4.5, handle 10.5,10.5 -> 14,14
    const qreal s = px / 16.0;
    p.drawEllipse(QPointF(7 * s, 7 * s), 4.5 * s, 4.5 * s);
    p.drawLine(QPointF(10.5 * s, 10.5 * s), QPointF(14 * s, 14 * s));
    p.end();
    return QIcon(pm);
}

QPixmap emptyGlyph(int px)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(kInk3), px * 0.06);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 28-unit glyph: rounded rect 4,6 20x16 r2.5; line 4,11->24,11; line 8,16->15,16
    const qreal s = px / 28.0;
    p.drawRoundedRect(QRectF(4 * s, 6 * s, 20 * s, 16 * s), 2.5 * s, 2.5 * s);
    p.drawLine(QPointF(4 * s, 11 * s), QPointF(24 * s, 11 * s));
    p.drawLine(QPointF(8 * s, 16 * s), QPointF(15 * s, 16 * s));
    p.end();
    return pm;
}

}  // namespace theme
