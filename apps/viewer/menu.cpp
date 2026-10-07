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

std::vector<MenuState::Row> MenuState::rows() const {
    if (testing())
        return {Row::Track, Row::Laps, Row::TyreLife, Row::Session, Row::TestCar, Row::TestLivery, Row::TestTyres,
                Row::TestFuel, Row::TestStats, Row::TestRuns, Row::Start};
    return {Row::Track, Row::Laps, Row::TyreLife, Row::Teams, Row::Drivers, Row::Session, Row::TyreRule, Row::Grid,
            Row::Stats, Row::Start};
}

int MenuState::rowOf(Row r) const {
    const std::vector<Row> rs = rows();
    for (int i = 0; i < (int)rs.size(); ++i)
        if (rs[i] == r) return i;
    return -1;
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

void MenuState::algoChanged(int car) {
    const int t = teamOfCar(car);
    if (t < 0) return;
    if ((int)styleCar.size() <= t) styleCar.resize(t + 1, -1);
    styleCar[t] = car;
}

int MenuState::stylesPending() const {
    int n = 0;
    for (int c : styleCar) n += c >= 0;
    return n;
}

void MenuState::applyStyles() {
    for (int t = 0; t < (int)styleCar.size(); ++t) {
        const int car = styleCar[t];
        // the car may have moved to another team since (a livery swap)
        if (car >= 0 && teamOfCar(car) == t) styleChanged(car);
        styleCar[t] = -1;
    }
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
        m.algoChanged(car);
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
    if (IsKeyPressed(KEY_A)) m.applyStyles();
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_BACKSPACE))
        m.gridPage = false;
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) { m.gridPage = false; break; }  // Done button
            if (h.row == 94) { m.applyStyles(); break; }     // style button
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

void changeTestStat(MenuState& m, int stat, int dir) {
    if (stat < 0 || stat >= (int)m.testStats.size()) return;
    int& v = m.testStats[stat];
    const StatRules& r = m.statRules;
    int sum = 0;
    for (int x : m.testStats) sum += x;
    if (dir > 0 && v < r.max && sum < r.budget) ++v;
    if (dir < 0 && v > r.min) --v;
}

MenuAction updateTestStats(MenuState& m, const std::vector<MenuHit>& hits) {
    const int n = std::max(1, (int)m.testStats.size());
    auto rep = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (rep(KEY_UP)) m.testStatRow = (m.testStatRow + n - 1) % n;
    if (rep(KEY_DOWN)) m.testStatRow = (m.testStatRow + 1) % n;
    if (rep(KEY_LEFT)) changeTestStat(m, m.testStatRow, -1);
    if (rep(KEY_RIGHT)) changeTestStat(m, m.testStatRow, 1);
    if (IsKeyPressed(KEY_D) && m.testAlgo < (int)m.algos.size()) m.testStats = m.statRules.parse(m.algos[m.testAlgo].stats);
    if (IsKeyPressed(KEY_ZERO)) m.testStats = m.statRules.parse("");
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_BACKSPACE))
        m.testStatsPage = false;
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) { m.testStatsPage = false; break; }
            if (h.row == 98 && m.testAlgo < (int)m.algos.size()) { m.testStats = m.statRules.parse(m.algos[m.testAlgo].stats); break; }
            if (h.row == 97) { m.testStats = m.statRules.parse(""); break; }
            if (h.row < 2000 || h.row >= 3000) continue;
            m.testStatRow = h.row - 2000;
            if (h.dir) changeTestStat(m, m.testStatRow, h.dir);
            break;
        }
    return MenuAction::None;
}

MenuAction updateRuns(MenuState& m, const std::vector<MenuHit>& hits) {
    const int n = (int)m.runLines.size();
    auto rep = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    MenuAction act = MenuAction::None;
    if (n > 0) {
        if (rep(KEY_UP)) m.runsRow = std::max(0, m.runsRow - 1);
        if (rep(KEY_DOWN)) m.runsRow = std::min(n - 1, m.runsRow + 1);
        if (rep(KEY_PAGE_UP)) m.runsRow = std::max(0, m.runsRow - 10);
        if (rep(KEY_PAGE_DOWN)) m.runsRow = std::min(n - 1, m.runsRow + 10);
        if (IsKeyPressed(KEY_HOME)) m.runsRow = 0;
        if (IsKeyPressed(KEY_END)) m.runsRow = n - 1;
    }
    m.runsRow = std::clamp(m.runsRow, 0, std::max(0, n - 1));
    auto pick = [&](MenuAction a) {
        if (m.runsRow >= n) return;
        if (a == MenuAction::ViewRun && !m.runLines[m.runsRow].telemetry) return;
        m.runPick = m.runLines[m.runsRow].id;
        act = a;
    };
    if (IsKeyPressed(KEY_S)) { m.runsSort = 1 - m.runsSort; m.runsRow = 0; }
    if (IsKeyPressed(KEY_T)) { m.runsAllTracks = !m.runsAllTracks; m.runsRow = 0; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) pick(MenuAction::LoadRun);
    if (IsKeyPressed(KEY_V)) pick(MenuAction::ViewRun);
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) m.runsPage = false;
    const float wheel = GetMouseWheelMove();
    if (wheel != 0) m.runsTop = std::max(0, m.runsTop - (int)wheel * 3);
    const Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (const MenuHit& h : hits) {
            if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h})) continue;
            if (h.row == 99) m.runsPage = false;
            else if (h.row == 98) pick(MenuAction::LoadRun);
            else if (h.row == 97) pick(MenuAction::ViewRun);
            else if (h.row == 96) { m.runsSort = 1 - m.runsSort; m.runsRow = 0; }
            else if (h.row == 95) { m.runsAllTracks = !m.runsAllTracks; m.runsRow = 0; }
            else if (h.row >= 3000) m.runsRow = h.row - 3000;
            break;
        }
    return act;
}

void change(MenuState& m, MenuState::Row row, int dir, bool big, MenuAction& act) {
    using Row = MenuState::Row;
    switch (row) {
        case Row::Track: {
            const int n = (int)m.tracks.size();
            m.track = (m.track + dir + n) % n;
            act = MenuAction::TrackChanged;
            break;
        }
        case Row::Laps: {
            const int step = big ? 10 : (m.laps >= 20 && dir > 0) || (m.laps > 20 && dir < 0) ? 5 : 1;
            m.laps = std::clamp(m.laps + dir * step, 1, 200);
            break;
        }
        case Row::TyreLife: m.tyreLife = std::clamp(m.tyreLife + dir, 0, MenuState::kNumTyreLives - 1); break;
        case Row::Teams:
            if (m.teamSlots.empty()) {
                m.cars = std::clamp(m.cars + dir, 1, m.maxCars);
            } else {
                m.teams = std::clamp(m.teams + dir, 1, (int)m.teamSlots.size());
                m.layoutGrid();
            }
            break;
        case Row::Drivers:
            if (!m.teamSlots.empty()) {
                m.drivers = 3 - m.drivers;
                m.layoutGrid();
            }
            break;
        case Row::Stats: m.teamsPage = true; break;
        case Row::Session: {
            m.session = (m.session + dir + 3) % 3;
            m.row = m.rowOf(Row::Session);
            break;
        }
        case Row::TyreRule: m.tyreRule = (m.tyreRule + dir + 3) % 3; break;
        case Row::Grid: m.gridPage = true; break;
        case Row::TestCar: {
            const int n = std::max(1, (int)m.algos.size());
            m.testAlgo = ((m.testAlgo + dir) % n + n) % n;
            break;
        }
        case Row::TestLivery: {
            const int n = std::max(1, m.liveryCount);
            m.testLivery = ((m.testLivery + dir) % n + n) % n;
            break;
        }
        case Row::TestTyres: m.testTires = ((m.testTires + dir) % 4 + 4) % 4; break;
        case Row::TestFuel: {
            const float step = big ? 5.0f : 1.0f;
            float f = m.testFuel > 0 ? m.testFuel : std::round(m.autoFuel);
            f += dir * step;
            m.testFuel = f < 1.0f ? 0.0f : std::min(f, m.tankLitres);  // below 1 L: back to automatic
            break;
        }
        case Row::TestStats: m.testStatsPage = true; break;
        case Row::TestRuns: m.runsPage = true; break;
        default: break;
    }
}

// Enter or a click on a row's value: open its page, or start.
void select(MenuState& m, MenuState::Row row, MenuAction& act) {
    using Row = MenuState::Row;
    switch (row) {
        case Row::Grid: m.gridPage = true; break;
        case Row::Stats: m.teamsPage = true; break;
        case Row::TestStats: m.testStatsPage = true; break;
        case Row::TestRuns: m.runsPage = true; break;
        case Row::TestFuel: m.testFuel = 0; break;  // back to automatic
        case Row::Start: act = MenuAction::Start; break;
        default: act = MenuAction::Start; break;
    }
}

}  // namespace

MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits) {
    if (m.gridPage) return updateGrid(m, hits);
    if (m.teamsPage) return updateTeams(m, hits);
    if (m.testStatsPage) return updateTestStats(m, hits);
    if (m.runsPage) return updateRuns(m, hits);
    using Row = MenuState::Row;
    const std::vector<Row> rows = m.rows();
    const int n = (int)rows.size();
    m.row = std::clamp(m.row, 0, n - 1);
    MenuAction act = MenuAction::None;
    const bool big = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) m.row = (m.row + n - 1) % n;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) m.row = (m.row + 1) % n;
    for (int key : {KEY_LEFT, KEY_RIGHT})
        if (IsKeyPressed(key) || IsKeyPressedRepeat(key)) change(m, rows[m.row], key == KEY_LEFT ? -1 : 1, big, act);
    if (IsKeyPressed(KEY_BACKSPACE) && rows[m.row] == Row::TestFuel) m.testFuel = 0;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
        const Row r = rows[m.row];
        if (r == Row::Grid || r == Row::Stats || r == Row::TestStats || r == Row::TestRuns) select(m, r, act);
        else act = MenuAction::Start;
    }
    if (IsKeyPressed(KEY_ESCAPE)) act = MenuAction::Quit;

    Vector2 mp = GetMousePosition();
    for (const MenuHit& h : hits) {
        if (!CheckCollisionPointRec(mp, {h.x, h.y, h.w, h.h}) || h.row < 0 || h.row >= n) continue;
        const Row r = rows[h.row];
        if (h.dir == 0 && r != Row::Start && GetMouseDelta().x * GetMouseDelta().x + GetMouseDelta().y * GetMouseDelta().y > 0)
            m.row = h.row;  // hovering a row selects it
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;
        m.row = h.row;
        if (h.dir != 0) change(m, r, h.dir, big, act);
        else if (r == Row::Start || r == Row::Grid || r == Row::Stats || r == Row::TestStats || r == Row::TestRuns)
            select(m, r, act);
    }
    return act;
}
