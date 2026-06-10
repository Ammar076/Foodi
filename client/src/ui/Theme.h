#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

// App-wide visual theme. Supports three modes (System / Light / Dark): the .qss is
// a template with @{token} colour placeholders that we substitute from a light or
// dark colour map at runtime, alongside a matching QPalette. Runtime-drawn marks
// (brand tile, glyphs) read their colours from the active map too.
namespace theme {

enum class Mode { System, Light, Dark };

// One-time setup: switch to the Fusion base style, install the persisted theme
// (palette + stylesheet + font + icon), and start following the OS theme when the
// mode is System. Call once at startup.
void apply(QApplication &app);

// Persist a new mode and re-apply it live to the running app.
void setMode(Mode mode);

// The user's persisted choice (defaults to System on first run).
Mode savedMode();

// The effective theme after resolving System against the OS (true = dark).
bool isDark();

// Active-theme colour for a token (e.g. "ink", "brand", "unsafeInk"). Used by the
// widgets we paint in C++ instead of QSS. Returns magenta if the token is unknown.
QColor color(const QString &token);

// Brand mark: a rounded teal tile with a white check, drawn at runtime so it stays
// crisp at any DPI. `px` is the logical edge length.
QPixmap appTile(int px);
QIcon appIcon();

// A leading magnifier glyph for the search field.
QIcon searchIcon(int px = 16);

// A muted "empty" glyph (card with lines) for the no-results state.
QPixmap emptyGlyph(int px = 36);

}  // namespace theme
