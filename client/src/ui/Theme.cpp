#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

// Thin wrapper over the base style that forces combo-box dropdowns to open as a
// plain list *below* the box (not the macOS-flavoured popup that aligns the
// selected item over the box and can spill off-screen).
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

using Map = QHash<QString, QString>;

// Light theme — the original warm-neutral / teal design tokens.
Map lightMap()
{
    return {
        {"brand", "#1a857a"},       {"brandHover", "#14716a"},   {"brandActive", "#0f5b54"},
        {"brandTint", "#e6f2f0"},   {"onBrand", "#ffffff"},
        {"page", "#f1efe9"},        {"surface", "#ffffff"},      {"surface2", "#f8f6f1"},
        {"surface3", "#efece5"},
        {"border", "#e4ded3"},      {"borderStrong", "#d6cfc2"}, {"borderHover", "#c6bfb1"},
        {"ink", "#2c2926"},         {"ink2", "#6c665d"},         {"ink3", "#9a948b"},
        {"safeBg", "#e7f4ec"},      {"safeInk", "#1c8048"},      {"safeBd", "#c3e3cf"},
        {"cautionBg", "#fcefe0"},   {"cautionInk", "#a65f0f"},   {"cautionBd", "#f3d9b3"},
        {"unsafeBg", "#fbeae7"},    {"unsafeInk", "#a5301f"},    {"unsafeBd", "#f0c8c1"},
        {"imgBg", "#f4f1ea"},       {"invalid", "#c0392b"},
        {"tooltipBg", "#2c2926"},   {"tooltipText", "#ffffff"},
    };
}

// Dark theme — warm-tinted dark surfaces (matches the warm neutral of the light
// theme) with a slightly brighter teal so the brand pops on dark, and softer
// badge colours tuned for contrast on dark backgrounds.
Map darkMap()
{
    return {
        {"brand", "#1f9b8e"},       {"brandHover", "#27ab9d"},   {"brandActive", "#178479"},
        {"brandTint", "#14302d"},   {"onBrand", "#ffffff"},
        {"page", "#1b1a18"},        {"surface", "#232220"},      {"surface2", "#2b2926"},
        {"surface3", "#343230"},
        {"border", "#3a3733"},      {"borderStrong", "#46423d"}, {"borderHover", "#565049"},
        {"ink", "#ece9e3"},         {"ink2", "#b3ada4"},         {"ink3", "#8b857c"},
        {"safeBg", "#16291f"},      {"safeInk", "#6cc394"},      {"safeBd", "#2a4636"},
        {"cautionBg", "#2e2316"},   {"cautionInk", "#e0a261"},   {"cautionBd", "#4a3922"},
        {"unsafeBg", "#2e1a17"},    {"unsafeInk", "#e88a7d"},    {"unsafeBd", "#4a2a25"},
        {"imgBg", "#2b2926"},       {"invalid", "#e0654f"},
        {"tooltipBg", "#46423d"},   {"tooltipText", "#ece9e3"},
    };
}

// Active state, set by applyActive().
Map g_active = lightMap();
theme::Mode g_mode = theme::Mode::System;

theme::Mode modeFromString(const QString &s)
{
    if (s == "light")
        return theme::Mode::Light;
    if (s == "dark")
        return theme::Mode::Dark;
    return theme::Mode::System;
}

QString modeToString(theme::Mode m)
{
    switch (m) {
        case theme::Mode::Light: return "light";
        case theme::Mode::Dark:  return "dark";
        default:                 return "system";
    }
}

// Resolve System against the OS colour scheme; Light/Dark pass through.
bool resolveDark(theme::Mode m)
{
    if (m == theme::Mode::Light)
        return false;
    if (m == theme::Mode::Dark)
        return true;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString buildStyleSheet(const Map &m)
{
    QFile qss(":/foodi.qss");
    if (!qss.open(QFile::ReadOnly | QFile::Text))
        return {};
    QString css = QString::fromUtf8(qss.readAll());
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        css.replace("@{" + it.key() + "}", it.value());
    return css;
}

QPalette buildPalette(const Map &m)
{
    auto c = [&](const char *k) { return QColor(m.value(k)); };
    QPalette pal;
    pal.setColor(QPalette::Window, c("page"));
    pal.setColor(QPalette::WindowText, c("ink"));
    pal.setColor(QPalette::Base, c("surface"));
    pal.setColor(QPalette::AlternateBase, c("surface2"));
    pal.setColor(QPalette::Text, c("ink"));
    pal.setColor(QPalette::Button, c("surface"));
    pal.setColor(QPalette::ButtonText, c("ink"));
    pal.setColor(QPalette::PlaceholderText, c("ink3"));
    pal.setColor(QPalette::Highlight, c("brand"));
    pal.setColor(QPalette::HighlightedText, c("onBrand"));
    pal.setColor(QPalette::ToolTipBase, c("tooltipBg"));
    pal.setColor(QPalette::ToolTipText, c("tooltipText"));
    pal.setColor(QPalette::Disabled, QPalette::Text, c("ink3"));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, c("ink3"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, c("ink3"));
    return pal;
}

void applyActive()
{
    g_active = resolveDark(g_mode) ? darkMap() : lightMap();
    qApp->setPalette(buildPalette(g_active));
    qApp->setStyleSheet(buildStyleSheet(g_active));
}

}  // namespace

namespace theme {

void apply(QApplication &app)
{
    // Fusion is fully QSS-friendly across platforms; the proxy adds the combo-popup
    // fix (it takes ownership of the Fusion instance).
    app.setStyle(new FoodiStyle(QStyleFactory::create("Fusion")));

    QFont f("Segoe UI");
    f.setPixelSize(13);
    app.setFont(f);

    QSettings s;
    g_mode = modeFromString(s.value("appearance/mode", "system").toString());
    applyActive();

    // Follow the OS theme live while in System mode.
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) {
                         if (g_mode == Mode::System)
                             applyActive();
                     });

    app.setWindowIcon(appIcon());
}

void setMode(Mode mode)
{
    g_mode = mode;
    QSettings s;
    s.setValue("appearance/mode", modeToString(mode));
    applyActive();
}

Mode savedMode() { return g_mode; }

bool isDark() { return resolveDark(g_mode); }

QColor color(const QString &token)
{
    return QColor(g_active.value(token, "#ff00ff"));
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
    p.fillPath(tile, color("brand"));

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
    QPen pen(color("ink3"), px * 0.1);
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
    QPen pen(color("ink3"), px * 0.06);
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
