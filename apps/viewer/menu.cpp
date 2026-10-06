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
        default: break;
    }
}

}  // namespace

MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits) {
    MenuAction act = MenuAction::None;
    const bool big = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) m.row = (m.row + MenuState::kRows - 1) % MenuState::kRows;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) m.row = (m.row + 1) % MenuState::kRows;
    for (int key : {KEY_LEFT, KEY_RIGHT})
        if (IsKeyPressed(key) || IsKeyPressedRepeat(key)) change(m, m.row, key == KEY_LEFT ? -1 : 1, big, act);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || (IsKeyPressed(KEY_SPACE) && m.row == 4))
        act = MenuAction::Start;
    if (IsKeyPressed(KEY_ESCAPE)) act = MenuAction::Quit;

    Vector2 mp = GetMousePosition();
    for (const MenuHit& h : hits) {
        if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
        if (h.dir == 0 && h.row != 4 && GetMouseDelta().x * GetMouseDelta().x + GetMouseDelta().y * GetMouseDelta().y > 0)
            m.row = h.row;  // hovering a row selects it
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;
        m.row = h.row;
        if (h.row == 4) act = MenuAction::Start;
        else if (h.dir != 0) change(m, h.row, h.dir, big, act);
    }
    return act;
}
