// The race-end screen: switchable windows over the finished race (results,
// positions gained and lost, the lap chart, lap times, tyre strategy and incidents).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hud.hpp"
#include "liveries.hpp"
#include "renderer.hpp"

namespace {

const Color kText = {238, 240, 245, 255};
const Color kDim = {160, 166, 180, 255};
const Color kFaint = {95, 100, 115, 255};
const Color kAccent = {255, 196, 40, 255};
const Color kBest = {190, 110, 255, 255};  // fastest lap, F1 purple
const Color kGood = {90, 200, 120, 255};
const Color kBad = {240, 90, 70, 255};

const char* kWindows[] = {"Results", "Positions", "Lap chart", "Lap times", "Strategy", "Incidents"};

// The race's fastest lap: car index and lap number (1-based), -1 if none.
void fastestLap(const rr::Race& race, int& car, int& lap) {
    car = lap = -1;
    float best = 0;
    for (int i = 0; i < (int)race.cars().size(); ++i) {
        const auto& t = race.cars()[i].lapTimes;
        for (int l = 0; l < (int)t.size(); ++l)
            if (t[l] > 0 && (car < 0 || t[l] < best)) best = t[l], car = i, lap = l + 1;
    }
}

int bestLapOf(const rr::Car& c) {
    int b = -1;
    for (int l = 0; l < (int)c.lapTimes.size(); ++l)
        if (c.lapTimes[l] > 0 && (b < 0 || c.lapTimes[l] < c.lapTimes[b])) b = l;
    return b + 1;
}

// Laps that say something about pace: not the standing start, not in or out of the pits.
std::vector<float> paceLaps(const rr::Car& c) {
    std::vector<float> v;
    for (int l = 1; l < (int)c.lapTimes.size(); ++l) {
        const int lap = l + 1;
        bool pit = false;
        for (int p : c.pitLaps) pit = pit || lap == p || lap == p + 1;
        if (!pit && c.lapTimes[l] > 0) v.push_back(c.lapTimes[l]);
    }
    return v;
}

// The compound of each stint and the lap it ended on.
struct Stint {
    int compound, from, to;
};
std::vector<Stint> stintsOf(const rr::Car& c, int laps) {
    std::vector<Stint> out;
    int comp = c.stopLog.empty() || c.stopLog[0].tiresBefore == 0 ? c.state.compound : c.stopLog[0].tiresBefore;
    int from = 0;
    for (const auto& s : c.stopLog) {
        if (s.tiresFitted == 0) continue;
        out.push_back({comp, from, s.lap});
        comp = s.tiresFitted;
        from = s.lap;
    }
    out.push_back({comp, from, std::max(from, std::min(laps, c.lapsDone))});
    return out;
}

const char* letter(int compound) { return compound == RR_TIRE_SOFT ? "S" : compound == RR_TIRE_HARD ? "H" : "M"; }

}  // namespace

int Hud::resultsTabAt(Vector2 p) const {
    for (int i = 0; i < (int)resultTabs_.size(); ++i)
        if (CheckCollisionPointRec(p, resultTabs_[i])) return i;
    return -1;
}

int Hud::resultsCarAt(Vector2 p, bool* inside) const {
    *inside = resultsBox_.width > 0 && CheckCollisionPointRec(p, resultsBox_);
    for (const RowHit& h : resultRows_)
        if (CheckCollisionPointRec(p, h.r)) return h.car;
    return -1;
}

void Hud::drawResults(const rr::Race& race, const HudState& st) {
    resultTabs_.clear();
    resultRows_.clear();
    resultsBox_ = {};
    if (!st.showResults || race.order().empty()) return;
    const auto& order = race.order();
    const auto& cars = race.cars();
    const int n = (int)order.size();
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    const float rowH = n > 16 ? 26.0f : 30.0f;
    const float w = std::min(1180.0f, sw - 32);
    const float h = std::min(sh - 24, 168 + rowH * n + (st.logPath.empty() ? 0 : 22));
    const float x = (sw - w) / 2, y = std::max(12.0f, (sh - h) / 2);
    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(Color{8, 10, 16, 255}, 0.35f));
    panel({x, y, w, h}, 0.92f);
    char buf[200];
    text("RACE RESULTS", x + 24, y + 16, 24, kAccent, true);
    std::snprintf(buf, sizeof buf, "%s   %d laps", race.track().name().c_str(), race.laps());
    text(buf, x + 24 + width("RACE RESULTS", 24, true) + 18, y + 21, 16, kDim);
    textRight("[ ] or click: window   G hide   R restart   Esc menu", x + w - 24, y + 22, 14, kDim);
    // tabs
    const int win = ((st.resultsWindow % 6) + 6) % 6;
    float tx = x + 24;
    for (int i = 0; i < 6; ++i) {
        std::snprintf(buf, sizeof buf, "%d  %s", i + 1, kWindows[i]);
        const float tw = width(buf, 15, true) + 26;
        const Rectangle r = {tx, y + 52, tw, 30};
        const bool on = i == win;
        DrawRectangleRounded(r, 0.3f, 6, on ? Fade(kAccent, 0.9f) : Fade(WHITE, 0.07f));
        text(buf, r.x + 13, r.y + 7, 15, on ? Color{20, 20, 24, 255} : kText, true);
        resultTabs_.push_back(r);
        tx += tw + 8;
    }
    // the fastest lap, on every window
    int fc = -1, fl = -1;
    fastestLap(race, fc, fl);
    if (fc >= 0) {
        std::snprintf(buf, sizeof buf, "FASTEST LAP  %s  %s (lap %d)", lapTime(cars[fc].lapTimes[fl - 1]).c_str(),
                      cars[fc].name.c_str(), fl);
        textRight(buf, x + w - 24, y + 59, 15, kBest, true);
    }
    const float top = y + 98, left = x + 24, right = x + w - 24;
    const float bodyBottom = y + h - (st.logPath.empty() ? 14 : 36);
    auto rowY = [&](int p) { return top + 24 + p * rowH; };
    resultsBox_ = {x, y, w, h};
    auto nameCell = [&](int p, int car, float nx) {
        std::snprintf(buf, sizeof buf, "%d", p + 1);
        textRight(buf, nx, rowY(p), 18, kText, true);
        DrawRectangle((int)nx + 10, (int)rowY(p) + 2, 5, (int)rowH - 8, teamColor(car));
        text(cars[car].name.c_str(), nx + 24, rowY(p), 18, car == st.focus ? kAccent : kText);
        resultRows_.push_back({{left, rowY(p) - 3, right - left, rowH}, car});
    };
    auto header = [&](const char* s, float hx, bool alignRight = false) {
        if (alignRight) textRight(s, hx, top, 13, kFaint, true);
        else text(s, hx, top, 13, kFaint, true);
    };
    const rr::Car& winner = cars[order[0]];

    switch (win) {
        case 0: {  // results
            header("POS", left);
            header("DRIVER", left + 70);
            header("LAPS", left + 420, true);
            header("TIME / GAP", left + 600, true);
            header("PITS", left + 680, true);
            header("BEST LAP", left + 820, true);
            header("PENALTY", right - 90, true);
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const rr::Car& c = cars[i];
                nameCell(p, i, left + 30);
                std::snprintf(buf, sizeof buf, "%d", c.lapsDone);
                textRight(buf, left + 420, rowY(p) + 1, 17, kDim, false, true);
                std::string t;
                if (c.dnf) t = "DNF";
                else if (!c.finished) t = "running";
                else if (p == 0) t = lapTime(c.raceTime());
                else if (c.lapsDone < winner.lapsDone) {
                    const int down = winner.lapsDone - c.lapsDone;
                    std::snprintf(buf, sizeof buf, "+%d lap%s", down, down == 1 ? "" : "s");
                    t = buf;
                } else t = "+" + lapTime(c.raceTime() - winner.raceTime());
                textRight(t.c_str(), left + 600, rowY(p) + 1, 17, c.dnf ? kBad : kText, false, true);
                std::snprintf(buf, sizeof buf, "%d", c.pitStops);
                textRight(buf, left + 680, rowY(p) + 1, 17, kDim, false, true);
                const bool fastest = i == fc;
                if (fastest) DrawRectangleRounded({left + 700, rowY(p) - 2, 128, rowH - 4}, 0.3f, 6, Fade(kBest, 0.25f));
                textRight(lapTime(c.bestLap).c_str(), left + 820, rowY(p) + 1, 17, fastest ? kBest : kDim, fastest, true);
                if (fastest) text("FASTEST", left + 834, rowY(p) + 3, 13, kBest, true);
                if (c.penalties > 0) {
                    std::snprintf(buf, sizeof buf, "%d  +%.0f s", c.penalties, c.penaltyTime);
                    textRight(buf, right - 90, rowY(p) + 1, 16, kBad, false, true);
                }
                if (c.twoCompoundPenalty) textRight("2-compound rule", right, rowY(p) + 3, 13, kBad);
                else if (c.dnf) {
                    std::string reason = c.dnfReason;
                    if (reason.size() > 14) reason = reason.substr(0, 13) + ".";
                    textRight(reason.c_str(), right, rowY(p) + 3, 13, kBad);
                }
            }
            break;
        }
        case 1: {  // positions: grid against finish
            header("FINISH", left);
            header("DRIVER", left + 70);
            header("GRID", left + 420, true);
            header("CHANGE", left + 520, true);
            header("GRID  >  FINISH", left + 580);
            const float gx = left + 580, gw = std::max(120.0f, right - 280 - gx);
            auto px = [&](int pos) { return gx + (n > 1 ? (pos - 1) * gw / (n - 1) : 0.0f); };
            int bestGain = 0, bestCar = -1, worstLoss = 0, worstCar = -1;
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const int grid = i + 1, fin = p + 1, d = grid - fin;
                nameCell(p, i, left + 30);
                std::snprintf(buf, sizeof buf, "P%d", grid);
                textRight(buf, left + 420, rowY(p) + 1, 17, kDim, false, true);
                if (d > 0) std::snprintf(buf, sizeof buf, "+%d", d);
                else if (d < 0) std::snprintf(buf, sizeof buf, "%d", d);
                else std::snprintf(buf, sizeof buf, "=");
                textRight(buf, left + 520, rowY(p) + 1, 17, d > 0 ? kGood : d < 0 ? kBad : kDim, true, true);
                // a strip from the grid slot to the finishing one
                const float cy = rowY(p) + rowH / 2 - 3;
                DrawLineEx({px(1), cy}, {px(n), cy}, 1, Fade(WHITE, 0.08f));
                DrawLineEx({px(grid), cy}, {px(fin), cy}, 4, Fade(d > 0 ? kGood : d < 0 ? kBad : kDim, 0.7f));
                DrawCircleV({px(grid), cy}, 4, kDim);
                DrawCircleV({px(fin), cy}, 5.5f, teamColor(i));
                if (d > bestGain) bestGain = d, bestCar = i;
                if (d < worstLoss) worstLoss = d, worstCar = i;
            }
            float sy = top + 24;
            const float sx = right - 250;
            text("MOST PLACES GAINED", sx, sy, 13, kFaint, true);
            text(bestCar >= 0 ? cars[bestCar].name.c_str() : "nobody", sx, sy + 18, 17, kText);
            if (bestCar >= 0) {
                std::snprintf(buf, sizeof buf, "+%d  (P%d to P%d)", bestGain, bestCar + 1, bestCar + 1 - bestGain);
                text(buf, sx, sy + 40, 16, kGood, true, true);
            }
            sy += 80;
            text("MOST PLACES LOST", sx, sy, 13, kFaint, true);
            text(worstCar >= 0 ? cars[worstCar].name.c_str() : "nobody", sx, sy + 18, 17, kText);
            if (worstCar >= 0) {
                std::snprintf(buf, sizeof buf, "%d  (P%d to P%d)", worstLoss, worstCar + 1, worstCar + 1 - worstLoss);
                text(buf, sx, sy + 40, 16, kBad, true, true);
            }
            sy += 80;
            const int pole = 0;  // cars are in grid order
            text("FROM POLE", sx, sy, 13, kFaint, true);
            const int polePos = (int)(std::find(order.begin(), order.end(), pole) - order.begin()) + 1;
            std::snprintf(buf, sizeof buf, "%s finished P%d", cars[pole].name.c_str(), polePos);
            text(buf, sx, sy + 18, 16, kText);
            break;
        }
        case 2: {  // lap chart: every car's position lap by lap
            int maxLap = 1;
            for (const rr::Car& c : cars) maxLap = std::max(maxLap, (int)c.lapPositions.size());
            const Rectangle pr = {left + 50, top + 10, right - 260 - (left + 50), bodyBottom - 34 - (top + 10)};
            auto P = [&](float lap, float pos) {
                return Vector2{pr.x + lap / maxLap * pr.width, pr.y + (n > 1 ? (pos - 1) / (n - 1) : 0.5f) * pr.height};
            };
            for (int pos = 1; pos <= n; ++pos) {
                const Vector2 a = P(0, (float)pos);
                DrawLineEx({pr.x, a.y}, {pr.x + pr.width, a.y}, 1, Fade(WHITE, 0.05f));
                std::snprintf(buf, sizeof buf, "P%d", pos);
                textRight(buf, pr.x - 10, a.y - 7, 12, kFaint, false, true);
            }
            const int step = maxLap > 40 ? 10 : maxLap > 15 ? 5 : 1;
            for (int l = 0; l <= maxLap; l += step) {
                const Vector2 a = P((float)l, 1);
                DrawLineEx({a.x, pr.y}, {a.x, pr.y + pr.height}, 1, Fade(WHITE, 0.05f));
                std::snprintf(buf, sizeof buf, "%d", l);
                text(buf, a.x - width(buf, 12, false, true) / 2, pr.y + pr.height + 8, 12, kFaint, false, true);
            }
            text("lap (0 = grid)", pr.x + pr.width - width("lap (0 = grid)", 12), pr.y + pr.height + 22, 12, kFaint);
            // the focused car last, on top
            std::vector<int> drawOrder;
            for (int i = 0; i < (int)cars.size(); ++i)
                if (i != st.focus) drawOrder.push_back(i);
            if (st.focus >= 0 && st.focus < (int)cars.size()) drawOrder.push_back(st.focus);
            for (int i : drawOrder) {
                const rr::Car& c = cars[i];
                const bool sel = i == st.focus;
                Color col = teamColor(i);
                if (!sel) col = Fade(col, 0.55f);
                Vector2 prev = P(0, (float)(i + 1));
                for (int l = 0; l < (int)c.lapPositions.size(); ++l) {
                    const Vector2 q = P((float)(l + 1), (float)c.lapPositions[l]);
                    DrawLineEx(prev, q, sel ? 4.0f : 2.0f, col);
                    prev = q;
                }
                if (c.dnf) DrawCircleLinesV(prev, 5, kBad);
            }
            // names down the right, in finishing order
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const float ly = P(0, (float)(p + 1)).y - 8;
                DrawRectangle((int)(pr.x + pr.width + 14), (int)ly + 2, 4, 13, teamColor(i));
                text(cars[i].name.c_str(), pr.x + pr.width + 24, ly, n > 16 ? 13 : 15, i == st.focus ? kAccent : kText);
                resultRows_.push_back({{pr.x + pr.width + 10, ly - 2, right - (pr.x + pr.width + 10), n > 16 ? 16.0f : 18.0f}, i});
            }
            textRight("Left / Right or 1-9 pick a car", right, bodyBottom - 14, 13, kDim);
            break;
        }
        case 3: {  // lap times
            header("POS", left);
            header("DRIVER", left + 70);
            header("BEST", left + 430, true);
            header("ON LAP", left + 500, true);
            header("AVERAGE", left + 600, true);
            header("SPREAD", left + 690, true);
            header("EVERY LAP (taller = slower than own best; lap 1, pit laps faint)", left + 720);
            const float sx0 = left + 720, sx1 = right;
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const rr::Car& c = cars[i];
                nameCell(p, i, left + 30);
                const int bl = bestLapOf(c);
                textRight(lapTime(c.bestLap).c_str(), left + 430, rowY(p) + 1, 17, i == fc ? kBest : kText, i == fc, true);
                std::snprintf(buf, sizeof buf, "%d", bl);
                if (bl > 0) textRight(buf, left + 500, rowY(p) + 1, 17, kDim, false, true);
                const std::vector<float> v = paceLaps(c);
                if (!v.empty()) {
                    float mean = 0, var = 0;
                    for (float t : v) mean += t;
                    mean /= v.size();
                    for (float t : v) var += (t - mean) * (t - mean);
                    textRight(lapTime(mean).c_str(), left + 600, rowY(p) + 1, 17, kDim, false, true);
                    std::snprintf(buf, sizeof buf, "%.2f s", std::sqrt(var / v.size()));
                    textRight(buf, left + 690, rowY(p) + 1, 16, kDim, false, true);
                }
                // each lap as a bar: taller is slower, 0 to 6% off the car's own best
                const int nl = (int)c.lapTimes.size();
                const float bw = nl > 0 ? std::min(40.0f, (sx1 - sx0 - 10) / std::max(1, race.laps())) : 0;
                for (int l = 0; l < nl; ++l) {
                    const float t = c.lapTimes[l];
                    if (t <= 0) continue;
                    const float off = std::clamp((t / std::max(1.0f, c.bestLap) - 1) / 0.06f, 0.0f, 1.0f);
                    const float bh = 3 + off * (rowH - 10);
                    bool pit = l == 0;
                    for (int pl : c.pitLaps) pit = pit || l + 1 == pl || l + 1 == pl + 1;
                    Color col = l + 1 == bl ? (i == fc ? kBest : kGood) : kDim;
                    if (pit) col = Fade(col, 0.3f);
                    DrawRectangle((int)(sx0 + 10 + l * bw), (int)(rowY(p) + rowH - 8 - bh), (int)std::max(1.0f, bw - 2), (int)bh,
                                  col);
                }
            }
            break;
        }
        case 4: {  // strategy: stints and stops
            header("POS", left);
            header("DRIVER", left + 70);
            header("STINTS", left + 330);
            header("STOPS", right - 170);
            header("PIT LANE", right, true);
            const float bx0 = left + 330, bx1 = right - 190;
            const int laps = std::max(1, race.laps());
            auto LX = [&](float lap) { return bx0 + lap / laps * (bx1 - bx0); };
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const rr::Car& c = cars[i];
                nameCell(p, i, left + 30);
                const float by = rowY(p) + 2, bh = rowH - 10;
                DrawRectangle((int)bx0, (int)by, (int)(bx1 - bx0), (int)bh, Fade(WHITE, 0.04f));
                std::string seq;
                for (const Stint& s : stintsOf(c, laps)) {
                    const float a = LX((float)s.from), b = LX((float)s.to);
                    DrawRectangleRounded({a + 1, by, std::max(2.0f, b - a - 2), bh}, 0.4f, 4, Fade(compoundColor(s.compound), 0.85f));
                    std::snprintf(buf, sizeof buf, "%s %d", letter(s.compound), s.to - s.from);
                    if (b - a > width(buf, 13, true) + 8)
                        text(buf, a + 6, by + bh / 2 - 7, 13, Color{20, 20, 24, 255}, true);
                    if (!seq.empty()) seq += "-";
                    seq += letter(s.compound);
                }
                // every stop, a repair-only one in red
                for (const auto& s : c.stopLog) {
                    const float sx = LX((float)s.lap);
                    DrawLineEx({sx, by - 3}, {sx, by + bh + 3}, 2, s.tiresFitted == 0 && s.repair ? kBad : kText);
                }
                std::snprintf(buf, sizeof buf, "%d  %s", c.pitStops, seq.c_str());
                text(buf, right - 170, rowY(p) + 1, 16, kText, false, true);
                std::snprintf(buf, sizeof buf, "%.1f s", c.pitLaneTime);
                textRight(buf, right, rowY(p) + 1, 16, kDim, false, true);
            }
            text("bars: tyre compound and laps on it   white line: stop   red line: stop without new tyres (repairs)", left,
                 bodyBottom - 14, 13, kDim);
            break;
        }
        default: {  // incidents
            header("POS", left);
            header("DRIVER", left + 70);
            header("CONTACTS", left + 410, true);
            header("PENALTIES", left + 500, true);
            header("BLUE FLAGS", left + 590, true);
            header("REPAIRS", left + 660, true);
            header("DAMAGE", left + 730, true);
            for (int p = 0; p < n; ++p) {
                const int i = order[p];
                const rr::Car& c = cars[i];
                nameCell(p, i, left + 30);
                auto num = [&](int v, float cx, Color hot) {
                    std::snprintf(buf, sizeof buf, "%d", v);
                    textRight(buf, cx, rowY(p) + 1, 17, v > 0 ? hot : kFaint, v > 0, true);
                };
                num(c.collisions, left + 410, kAccent);
                num(c.penalties, left + 500, kBad);
                num(c.blueFlags, left + 590, Color{80, 140, 255, 255});
                int repairs = 0;
                for (const auto& s : c.stopLog) repairs += s.repair;
                num(repairs, left + 660, kAccent);
                const float dmg = 100 * std::min(1.0f, c.state.damage / std::max(1.0f, c.phys.damageForMaxLoss));
                std::snprintf(buf, sizeof buf, "%.0f%%", dmg);
                textRight(buf, left + 730, rowY(p) + 1, 17, dmg > 30 ? kBad : dmg > 5 ? kAccent : kFaint, false, true);
            }
            // the hardest contacts of the race
            const float cx = left + 770;
            text("HARDEST CONTACTS", cx, top, 13, kFaint, true);
            // penalties (with why) take the bottom of the column
            std::vector<std::string> pens;
            for (int p = 0; p < n; ++p)
                for (const auto& pl : cars[order[p]].penaltyLog) {
                    // the log has the full reason; here its kind ("blue flag", "two-compound rule")
                    const std::string kind = pl.reason.substr(0, pl.reason.find(':'));
                    std::snprintf(buf, sizeof buf, "%s   lap %d   +%.0f s   %s", cars[order[p]].name.c_str(), pl.lap, pl.seconds,
                                  kind.c_str());
                    pens.push_back(buf);
                }
            const int penRows = std::min<int>((int)pens.size(), 5);
            const float penTop = bodyBottom - 6 - (penRows ? 22 + penRows * 18.0f : 0);
            if (penRows) {
                text("PENALTIES", cx, penTop, 13, kFaint, true);
                for (int k = 0; k < penRows; ++k) {
                    std::string line = k == 4 && pens.size() > 5 ? "... and " + std::to_string(pens.size() - 4) + " more" : pens[k];
                    float size = 14;
                    while (size > 10 && width(line.c_str(), size) > right - cx) size -= 1;
                    text(line.c_str(), cx, penTop + 20 + k * 18, size, kBad);
                }
            }
            std::vector<rr::Contact> hits = race.contacts();
            std::sort(hits.begin(), hits.end(), [](const rr::Contact& a, const rr::Contact& b) { return a.speed > b.speed; });
            const int rows = std::min((int)hits.size(), std::max(0, (int)((penTop - top - 30) / rowH)));
            if (hits.empty()) text("a clean race: no contact at all", cx, top + 24, 16, kDim);
            for (int k = 0; k < rows; ++k) {
                const rr::Contact& h = hits[k];
                const float ry = rowY(k);
                textRight(lapTime(h.time).c_str(), cx + 82, ry + 1, 15, kDim, false, true);
                std::snprintf(buf, sizeof buf, "%.0f km/h", h.speed * 3.6f);
                textRight(buf, cx + 170, ry + 1, 15, h.speed > 8 ? kBad : kAccent, true, true);
                std::snprintf(buf, sizeof buf, "%s into %s", cars[h.a].name.c_str(), cars[h.b].name.c_str());
                float size = 15;
                while (size > 10 && width(buf, size) > right - cx - 184) size -= 1;
                text(buf, cx + 184, ry + 1, size, kText);
            }
            break;
        }
    }
    if (!st.logPath.empty()) {
        const std::string line = "Race log: " + st.logPath;
        float size = 13;
        while (size > 9 && width(line.c_str(), size) > w - 48) size -= 1;
        text(line.c_str(), x + 24, y + h - 26, size, kDim);
    }
}
