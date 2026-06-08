#pragma once
#include <QLabel>
#include <QString>

// The safety badge — the product's single most distinctive component. Maps the
// backend's per-user status ("UNSAFE"/"CAUTION"/"SAFE") to the design's pill:
// tinted background + matching border + a leading glyph + the word (so it never
// relies on colour alone). Visuals live in foodi.qss (QLabel#badge[state=...]).
namespace badge {

inline QString stateKey(const QString &status)
{
    if (status == "UNSAFE")
        return QStringLiteral("unsafe");
    if (status == "CAUTION")
        return QStringLiteral("caution");
    return QStringLiteral("safe");
}

inline QString text(const QString &status)
{
    if (status == "UNSAFE")
        return QStringLiteral("✕  Unsafe");   // ✕
    if (status == "CAUTION")
        return QStringLiteral("!  Caution");
    return QStringLiteral("✓  Safe");          // ✓
}

// A styled pill with explicit state ("safe"/"caution"/"unsafe"/"info") and text.
inline QLabel *pill(const QString &state, const QString &text, QWidget *parent = nullptr)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setObjectName("badge");
    lbl->setProperty("state", state);
    lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    return lbl;
}

// Build the safety badge for `status`. `large` selects the detail-modal size.
inline QLabel *make(const QString &status, bool large = false, QWidget *parent = nullptr)
{
    auto *lbl = pill(stateKey(status), text(status), parent);
    if (large)
        lbl->setProperty("size", "lg");
    return lbl;
}

}  // namespace badge
