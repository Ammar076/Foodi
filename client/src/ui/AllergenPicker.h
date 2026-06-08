#pragma once
#include <QList>
#include <QWidget>

class QCheckBox;
class QGridLayout;
class QJsonArray;
template <typename T>
class QSet;

// A two-column checklist of allergens. Pure UI: the parent fetches the allergen
// list and the user's current selection, hands them in, and reads back the
// checked ids. Shared by the onboarding page and the profile page.
class AllergenPicker : public QWidget
{
    Q_OBJECT
public:
    explicit AllergenPicker(QWidget *parent = nullptr);

    // Build a checkbox per allergen in `all` ({id, display_name, ...}), checking
    // those whose id is in `selected`. Replaces any existing checkboxes.
    void setAllergens(const QJsonArray &all, const QSet<int> &selected);

    QList<int> selectedIds() const;

signals:
    void selectionChanged();

private:
    QGridLayout *grid_ = nullptr;
    QList<QCheckBox *> boxes_;  // each carries its allergen id in an "allergenId" property
};
