#include "menu.hpp"

#include <cstdlib>
#include <sstream>

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

std::vector<int> StatRules::parse(const std::string& dev) const {
    std::vector<int> v(keys.size(), neutral);
    std::stringstream ss(dev);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        for (size_t k = 0; k < keys.size(); ++k)
            if (keys[k] == item.substr(0, eq)) v[k] = std::clamp(std::atoi(item.c_str() + eq + 1), min, max);
    }
    return v;
}

std::string StatRules::format(const std::vector<int>& v) const {
    std::string out;
    for (size_t k = 0; k < keys.size() && k < v.size(); ++k) {
        if (v[k] == neutral) continue;
        if (!out.empty()) out += ",";
        out += keys[k] + "=" + std::to_string(v[k]);
    }
    return out;
}

std::vector<int> MenuState::raceTeams() const {
    std::vector<int> out;
    for (int car = 0; car < cars; ++car) {
        const int t = teamOfCar(car);
        if (t >= 0 && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    return out;
}

void MenuState::layoutGrid() {
    if (teamSlots.empty()) return;
    teams = std::clamp(teams, 1, (int)teamSlots.size());
    drivers = std::clamp(drivers, 1, 2);
    cars = std::min(maxCars, teams * drivers);
    std::vector<int> slots;
    for (int d = 0; d < drivers; ++d)
        for (int t = 0; t < teams; ++t) slots.push_back(teamSlots[t][std::min<size_t>(d, teamSlots[t].size() - 1)]);
    // the slots nobody races keep their places after the grid (for livery swaps)
    for (int slot = 0; slot < liveryCount; ++slot)
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
    slots.resize(std::max<size_t>(slots.size(), carLivery.size()), 0);
    for (size_t i = 0; i < carLivery.size(); ++i) carLivery[i] = slots[i];
    gridRow = std::min(gridRow, cars - 1);
}

void MenuState::styleChanged(int car) {
    const int t = teamOfCar(car);
    if (t < 0 || t >= (int)teamStats.size() || car >= (int)carAlgo.size()) return;
    const int a = carAlgo[car];
    if (a >= 0 && a < (int)algos.size()) teamStats[t] = statRules.parse(algos[a].stats);
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
    } else if (col == 1) {
        const int n = (int)m.algos.size();
        m.carAlgo[car] = ((m.carAlgo[car] + dir) % n + n) % n;
        m.styleChanged(car);
    } else if (car < (int)m.carTires.size()) {
        m.carTires[car] = ((m.carTires[car] + dir) % 4 + 4) % 4;  // auto, soft, medium, hard
    }
}

MenuAction updateGrid(MenuState& m, const std::vector<MenuHit>& hits) {
    const int n = m.cars;
    auto rep = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (rep(KEY_UP)) m.gridRow = (m.gridRow + n - 1) % n;
    if (rep(KEY_DOWN)) m.gridRow = (m.gridRow + 1) % n;
    if (IsKeyPressed(KEY_TAB)) m.gridCol = (m.gridCol + 1) % MenuState::kGridCols;
    if (rep(KEY_LEFT)) changeGrid(m, m.gridRow, m.gridCol, -1);
    if (rep(KEY_RIGHT)) changeGrid(m, m.gridRow, m.gridCol, 1);
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_BACKSPACE))
        m.gridPage = false;
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) { m.gridPage = false; break; }  // Done button
            const int car = (h.row - 100) / MenuState::kGridCols, col = (h.row - 100) % MenuState::kGridCols;
            m.gridRow = car;
            m.gridCol = col;
            if (h.dir) changeGrid(m, car, col, h.dir);
        }
    return MenuAction::None;
}

void changeStat(MenuState& m, int team, int stat, int dir) {
    if (team < 0 || team >= (int)m.teamStats.size() || stat < 0 || stat >= (int)m.teamStats[team].size()) return;
    int& v = m.teamStats[team][stat];
    const StatRules& r = m.statRules;
    if (dir > 0 && v < r.max && m.statSum(team) < r.budget) ++v;
    if (dir < 0 && v > r.min) --v;
}

MenuAction updateTeams(MenuState& m, const std::vector<MenuHit>& hits) {
    const std::vector<int> teams = m.raceTeams();
    const int n = std::max(1, (int)teams.size()), ns = std::max(1, (int)m.statRules.keys.size());
    auto rep = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    m.teamRow = std::min(m.teamRow, n - 1);
    if (rep(KEY_UP)) m.teamRow = (m.teamRow + n - 1) % n;
    if (rep(KEY_DOWN)) m.teamRow = (m.teamRow + 1) % n;
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (IsKeyPressed(KEY_TAB)) m.statCol = (m.statCol + (shift ? ns - 1 : 1)) % ns;
    const int team = m.teamRow < (int)teams.size() ? teams[m.teamRow] : -1;
    if (rep(KEY_LEFT)) changeStat(m, team, m.statCol, -1);
    if (rep(KEY_RIGHT)) changeStat(m, team, m.statCol, 1);
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_BACKSPACE))
        m.teamsPage = false;
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) { m.teamsPage = false; break; }
            const int t = (h.row - 1000) / 16, stat = (h.row - 1000) % 16;
            for (int i = 0; i < (int)teams.size(); ++i)
                if (teams[i] == t) m.teamRow = i;
            m.statCol = stat;
            if (h.dir) changeStat(m, t, stat, h.dir);
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
        case MenuState::kTeamsRow:
            if (m.teamSlots.empty()) {
                m.cars = std::clamp(m.cars + dir, 1, m.maxCars);
            } else {
                m.teams = std::clamp(m.teams + dir, 1, (int)m.teamSlots.size());
                m.layoutGrid();
            }
            break;
        case MenuState::kDriversRow:
            if (!m.teamSlots.empty()) {
                m.drivers = 3 - m.drivers;
                m.layoutGrid();
            }
            break;
        case MenuState::kStatsRow: m.teamsPage = true; break;
        case MenuState::kSessionRow: m.weekend = !m.weekend; break;
        case MenuState::kRuleRow: m.tyreRule = (m.tyreRule + dir + 3) % 3; break;
        case MenuState::kGridRow: m.gridPage = true; break;
        default: break;
    }
}

}  // namespace

MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits) {
    if (m.gridPage) return updateGrid(m, hits);
    if (m.teamsPage) return updateTeams(m, hits);
    MenuAction act = MenuAction::None;
    const bool big = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) m.row = (m.row + MenuState::kRows - 1) % MenuState::kRows;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) m.row = (m.row + 1) % MenuState::kRows;
    for (int key : {KEY_LEFT, KEY_RIGHT})
        if (IsKeyPressed(key) || IsKeyPressedRepeat(key)) change(m, m.row, key == KEY_LEFT ? -1 : 1, big, act);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (m.row == MenuState::kGridRow) m.gridPage = true;
        else if (m.row == MenuState::kStatsRow) m.teamsPage = true;
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
        else if (h.row == MenuState::kGridRow && h.dir == 0) m.gridPage = true;
        else if (h.row == MenuState::kStatsRow && h.dir == 0) m.teamsPage = true;
        else if (h.dir != 0) change(m, h.row, h.dir, big, act);
    }
    return act;
}
