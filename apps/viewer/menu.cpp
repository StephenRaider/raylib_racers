#include "menu.hpp"

#include "raylib.h"

constexpr int MenuState::kTyreLives[];

void MenuState::setWearRate(float rate) {
    if (rate <= 0) {
        tyreLife = kNumTyreLives - 1;
        return;
    }
    const float want = baseTyreLife() / rate;
    int best = 0;
    for (int i = 0; i + 1 < kNumTyreLives; ++i)
        if (std::fabs(std::log(kTyreLives[i] / want)) < std::fabs(std::log(kTyreLives[best] / want))) best = i;
    tyreLife = best;
}

namespace {

// Picks a livery for a car; a livery another car wears is swapped with it.
void setLivery(MenuState& m, int car, int slot) {
    const int n = std::max(1, m.liveryCount);
    slot = ((slot % n) + n) % n;
    for (int i = 0; i < (int)m.carLivery.size(); ++i)
        if (i != car && m.carLivery[i] == slot) m.carLivery[i] = m.carLivery[car];
    m.carLivery[car] = slot;
}

void changeGrid(MenuState& m, int car, int col, int dir) {
    if (car < 0 || car >= (int)m.carLivery.size()) return;
    if (col == 0) {
        setLivery(m, car, m.carLivery[car] + dir);
    } else {
        const int n = (int)m.algos.size();
        m.carAlgo[car] = ((m.carAlgo[car] + dir) % n + n) % n;
    }
}

MenuAction updateGrid(MenuState& m, const std::vector<MenuHit>& hits) {
    const int n = m.cars;
    auto rep = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (rep(KEY_UP)) m.gridRow = (m.gridRow + n - 1) % n;
    if (rep(KEY_DOWN)) m.gridRow = (m.gridRow + 1) % n;
    if (IsKeyPressed(KEY_TAB)) m.gridCol ^= 1;
    if (rep(KEY_LEFT)) changeGrid(m, m.gridRow, m.gridCol, -1);
    if (rep(KEY_RIGHT)) changeGrid(m, m.gridRow, m.gridCol, 1);
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_BACKSPACE))
        m.gridPage = false;
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) { m.gridPage = false; break; }  // Done button
            const int car = (h.row - 100) / 2, col = (h.row - 100) % 2;
            m.gridRow = car;
            m.gridCol = col;
            if (h.dir) changeGrid(m, car, col, h.dir);
        }
    return MenuAction::None;
}

void change(MenuState& m, int row, int dir, bool big, MenuAction& act) {
    switch (row) {
        case 0: {
            const int n = (int)m.tracks.size();
            m.track = (m.track + dir + n) % n;
            act = MenuAction::TrackChanged;
            break;
        }
        case 1: {
            const int step = big ? 10 : (m.laps >= 20 && dir > 0) || (m.laps > 20 && dir < 0) ? 5 : 1;
            m.laps = std::clamp(m.laps + dir * step, 1, 200);
            break;
        }
        case 2: m.tyreLife = std::clamp(m.tyreLife + dir, 0, MenuState::kNumTyreLives - 1); break;
        case 3: m.cars = std::clamp(m.cars + dir, 1, m.maxCars); break;
        case MenuState::kSessionRow: m.weekend = !m.weekend; break;
        case MenuState::kGridRow: m.gridPage = true; break;
        default: break;
    }
}

}  // namespace

MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits) {
    if (m.gridPage) return updateGrid(m, hits);
    MenuAction act = MenuAction::None;
    const bool big = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) m.row = (m.row + MenuState::kRows - 1) % MenuState::kRows;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) m.row = (m.row + 1) % MenuState::kRows;
    for (int key : {KEY_LEFT, KEY_RIGHT})
        if (IsKeyPressed(key) || IsKeyPressedRepeat(key)) change(m, m.row, key == KEY_LEFT ? -1 : 1, big, act);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (m.row == MenuState::kGridRow) m.gridPage = true;
        else act = MenuAction::Start;
    }
    if (IsKeyPressed(KEY_ESCAPE)) act = MenuAction::Quit;

    Vector2 mp = GetMousePosition();
    for (const MenuHit& h : hits) {
        if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
        if (h.dir == 0 && h.row != MenuState::kStartRow && GetMouseDelta().x * GetMouseDelta().x + GetMouseDelta().y * GetMouseDelta().y > 0)
            m.row = h.row;  // hovering a row selects it
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;
        m.row = h.row;
        if (h.row == MenuState::kStartRow) act = MenuAction::Start;
        else if (h.row == MenuState::kGridRow) m.gridPage = true;
        else if (h.dir != 0) change(m, h.row, h.dir, big, act);
    }
    return act;
}
