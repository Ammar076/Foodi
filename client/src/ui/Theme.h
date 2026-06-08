#pragma once
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

// App-wide visual theme. Centralises the design tokens that can't live in the
// .qss (the runtime-drawn brand mark and search glyph) and the one-time setup
// that installs the stylesheet. Colours mirror resources/foodi.qss.
namespace theme {

// Brand palette (kept in sync with foodi.qss / the design's :root).
inline const char *kBrand = "#1a857a";
inline const char *kInk3 = "#9a948b";

// Switch to the Fusion base style (so QSS is honoured uniformly across
// platforms), set the UI font, and load resources/foodi.qss onto the app.
void apply(QApplication &app);

// The Foodi mark: a rounded teal tile with a white check. Drawn at runtime so it
// stays crisp at any DPI and needs no PNG asset. `px` is the logical edge length.
QPixmap appTile(int px);
QIcon appIcon();

// A leading magnifier glyph for the search field.
QIcon searchIcon(int px = 16);

// A muted "empty" glyph (card with lines) for the no-results state.
QPixmap emptyGlyph(int px = 36);

}  // namespace theme
