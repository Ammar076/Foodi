# Foodi — Code Review & GUI Feasibility Report

**Project:** Foodi — a console-based Food Allergen Checker
**Language:** C++ (single file, `Foodi.cpp`, ~746 lines)
**Reviewed:** 2026-06-06

---

## 1. What the program does

Foodi is a console application that lets a user:

- Create / select / edit / delete **user profiles**, each with a name, email, and a list of **allergens** (persisted to `profiles.txt`).
- Browse a catalogue of **food items** loaded from `fooditems.txt`, filtered by *diet*, *cuisine*, or *category*.
- Add new food items to the catalogue.
- **Cross-check** a chosen food item's ingredients against the current user's allergens and report any matches.

The design uses classes (`Allergen`, `User`, `Cuisine`, `FoodCategory` + subclasses, `Diet`, `FoodItem`), inheritance, an abstract base class, and overloaded `<<` / `>>` operators — a solid OOP learning project.

---

## 2. Can it have a GUI? — Short answer: **Yes, definitely.**

The domain logic (profiles, food items, allergen matching) maps very naturally onto GUI widgets: forms for profiles, a table/list for food items, filter dropdowns, and a results panel.

**However**, the code is *not* GUI-ready as written. Console input/output (`cin` / `cout`) is woven directly into the business logic — including inside the `operator>>` overload, `createNewProfile`, `searchFoodItems`, etc. A GUI cannot call functions that block on `cin` and print prompts to `cout`. So the real work is **refactoring the logic apart from the I/O**, not the GUI toolkit itself. See sections 5 and 6 for a concrete path.

---

## 3. Strengths

- Clean use of OOP: encapsulation, an abstract `FoodCategory` base, inheritance (`FoodItem : Cuisine`), and operator overloading.
- Reasonable separation of *functions* by responsibility (load, save, search, manage).
- Exceptions are used for file errors and mostly caught at call sites.
- Const-correctness on most getters.
- The flat-file persistence format is simple and human-readable.

---

## 4. Issues found

Severity: 🔴 Critical · 🟠 High · 🟡 Medium · ⚪ Low

### 🔴 Infinite loop on non-numeric menu input
`Foodi.cpp:670` (`displayMenu`) and `Foodi.cpp:252` (`profileManagement`)

```cpp
cin >> choice;   // if the user types a letter, extraction fails
cin.ignore();    // no-op while the stream is in a fail state
```

If the user types a non-number, `cin` sets `failbit`, `choice` becomes `0`, and the bad characters are **never consumed** (`cin.ignore()` does nothing once the stream has failed). The next loop iteration fails again immediately → the menu spins forever printing the error.
**Fix:** check the stream and clear it:
```cpp
if (!(cin >> choice)) {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cout << "Please enter a number.\n";
    continue;
}
cin.ignore(numeric_limits<streamsize>::max(), '\n');
```

### 🔴 Uninitialized variable used as a loop bound
`Foodi.cpp:408-419` (`operator>>` for `FoodItem`)

```cpp
int n;                       // uninitialized
try {
    cout << "Enter number of ingredients: ";
    is >> n;                 // streams don't throw by default -> catch never fires
}
catch (exception& e) { ... }
...
for (int i = 0; i < n; ++i) // if extraction failed, n is garbage -> UB
```

The `try/catch` is ineffective because stream extraction doesn't throw unless exceptions are explicitly enabled. On bad input `n` is undefined and drives the loop.
**Fix:** initialize (`int n = 0;`) and validate the read instead of relying on `try/catch`.

### 🟠 `addAllergy` replaces instead of adds (and is misnamed)
`Foodi.cpp:50-52`

```cpp
void addAllergy(vector<Allergen> _allergens) {
    allergies = _allergens;   // overwrites the whole list
}
```

The name says "add" but it assigns. Rename to `setAllergies`, or implement true append (`allergies.push_back(...)`).

### 🟠 Spaces in names / cuisines / ingredients corrupt the data file
`Foodi.cpp:400-424` (input via `getline`) vs. `Foodi.cpp:485-519` (load splits on spaces)

The `fooditems.txt` format is space-delimited, so each field must be a single token. But `operator>>` reads name, cuisine, and ingredients with `getline`, which accepts spaces. Entering `ice cream` saves `... ice cream ...`, and the loader then reads it back as **two** ingredients. (The seed data dodges this with underscores: `ice_cream`, `soy_sauce`, `vanilla_extract`, `middle_eastern`.)
**Fix:** either validate/replace spaces on input, or switch to a delimiter-safe format (one field per line, or CSV/JSON with quoting).

### 🟠 Last record is lost without a trailing blank line
`Foodi.cpp:117-130` (`loadAllProfiles`)

A `User` is only pushed when the parser hits an **empty line**. If `profiles.txt` doesn't end with a blank line, the final profile is silently dropped. It works today only because `saveUserData` happens to append a trailing blank line — fragile coupling between writer and reader.
**Fix:** also flush a pending record at EOF.

### 🟡 Repeated full-file reads
`isProfileFound`, `loadProfileByName`, `displayAllProfiles`, `deleteProfile`, `editProfile` each call `loadAllProfiles()` from scratch — and some flows call several in a row. Fine at this scale, but it's O(file) per lookup. Load once and pass the collection around.

### 🟡 `exit(0)` skips all cleanup
`Foodi.cpp:715` — choosing "Exit" calls `exit(0)`, bypassing destructors and stack unwinding. Prefer returning out of the loop normally. (Also makes the outer `while(true)` in `main` at `Foodi.cpp:737` effectively dead code.)

### 🟡 Invalid category/diet silently persisted
`Foodi.cpp:441-444, 461-464` — an unrecognized category sets `nullptr` → `getCategory()` returns `"No_category"`, which is then written to the file and won't parse back into any real category on reload. Reject the input or re-prompt instead.

### 🟡 No duplicate-profile protection
`createNewProfile` (`Foodi.cpp:83`) never checks whether the name already exists. Two profiles can share a name; `loadProfileByName` returns the first, `deleteProfile` removes all matches.

### 🟡 Allergen matching is exact-token only
`FoodItem_Has_Allergen` (`Foodi.cpp:557`) compares whole ingredient tokens. An allergy of `soy` will **not** match the ingredient `soy_sauce`, and `egg` won't match `eggs`. Consider substring/normalized matching for real-world use.

### ⚪ Containers returned by value, then copied in hot loops
`getAllergies()` / `getIngredients()` return by value, and `FoodItem_Has_Allergen` calls them inside nested loops (`Foodi.cpp:562-564`) — re-copying the vectors every iteration. Return `const&` and/or hoist the calls out of the loops.

### ⚪ Style / structure
- `using namespace std;` at global scope (`Foodi.cpp:7`).
- Entire file is indented one extra level (everything sits inside 4 leading spaces).
- I/O concerns live inside `operator>>` (it prints prompts) — operators should not drive interactive I/O.
- Everything is in one `.cpp`; there's no `.h`/`.cpp` split and no build file (no `CMakeLists.txt` / `Makefile`).
- `Allergen` and `Diet` are thin wrappers around a single `string` — acceptable for an OOP exercise, but they could be plain strings.
- Mixed `cin` / `is` usage inside `operator>>` (`Foodi.cpp:416`).
- No unit tests.

---

## 5. Recommended GUI options

All of these are viable on your Windows setup:

| Option | Effort | Look & feel | Notes |
|--------|--------|-------------|-------|
| **Qt Widgets** ⭐ recommended | Medium | Native, polished | Best documented, cross-platform, great tooling (Qt Designer). Ideal for form + table UIs like this. |
| **Dear ImGui** | Low | Functional/"tool" style | Fastest to wire up, drops into a single window; less native-looking. Great if you just want it working quickly. |
| **wxWidgets** | Medium | Native | Native widgets on each OS; heavier setup than Qt. |
| **FLTK** | Low–Med | Basic | Lightweight, small dependency. |
| **Win32 / MFC** | Med–High | Native (Windows only) | Ties you to Windows; more boilerplate. |
| **Wt (web UI in C++)** / rewrite as web | High | Browser | Overkill here, but an option if you want it online. |

**Recommendation:** **Qt Widgets** for a real desktop app, or **Dear ImGui** if you want the quickest possible win. Either way, the GUI layer is small — the prerequisite refactor (below) is the bulk of the effort.

---

## 6. Refactor roadmap to make a GUI possible

The single most important change: **separate the domain/logic from the console I/O.** Once functions take parameters and return values (instead of reading `cin` / writing `cout`), any GUI can call them.

1. **Split into layers / files**
   - `model.h/.cpp` — `Allergen`, `User`, `FoodItem`, `Diet`, categories (pure data + behavior, no I/O).
   - `storage.h/.cpp` — load/save for `profiles.txt` and `fooditems.txt`, returning/accepting collections.
   - `service.h/.cpp` — operations like `vector<FoodItem> filter(...)`, `vector<string> findAllergens(const FoodItem&, const User&)`, `addFoodItem(...)`, `createProfile(...)`. **No `cin`/`cout`.**
   - `main_console.cpp` — the existing text menu, now just calling the service layer.
   - `main_gui.cpp` — the new GUI, calling the *same* service layer.

2. **Remove I/O from `operator>>`** — replace it with a plain constructor / setters that accept already-collected values. The GUI gathers input from text fields; the console gathers it from `cin`. The model shouldn't care which.

3. **Return results instead of printing them** — e.g. `findAllergens()` returns `vector<string>`; the caller decides whether to `cout` it or show it in a label.

4. **Add a build file** — a `CMakeLists.txt` makes it straightforward to build both the console and GUI targets and to pull in Qt/ImGui.

5. **Then build the GUI** — a main window with: a profile selector + "New/Edit/Delete" form; a food-items table with diet/cuisine/category filter dropdowns; and an "allergen check" result panel that turns red/green based on `findAllergens()`.

Mapping to widgets:
- Profile list → `QListWidget` / combo box.
- New/Edit profile → form with `QLineEdit` (name, email) + an editable allergen list.
- Food items → `QTableWidget` with filter `QComboBox`es above it.
- Allergen check → select a row, click "Check", show matches in a colored banner.

---

## 7. Suggested next steps (priority order)

1. Fix the two 🔴 crashes/loops (non-numeric input; uninitialized `n`). These bite users immediately.
2. Fix `addAllergy` semantics and the space-in-field data corruption (🟠).
3. Add a `CMakeLists.txt` and split logic from I/O (section 6, steps 1–3).
4. Pick a toolkit (Qt recommended) and build a minimal window over the service layer.
5. Add a few unit tests around `filter` and `findAllergens` so the GUI and console share verified logic.

**Build today (no GUI):**
```powershell
g++ -std=c++17 Foodi.cpp -o foodi.exe
.\foodi.exe
```
