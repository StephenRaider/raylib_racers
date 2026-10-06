#include "hud.hpp"

#include "liveries.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

const Color kText = {238, 240, 245, 255};
const Color kDim = {160, 166, 180, 255};
const Color kAccent = {255, 196, 40, 255};
const Color kBlueFlag = {40, 110, 255, 255};

std::string lapTime(double t) {
    if (t <= 0) return "-:--.---";
    int m = (int)(t / 60);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%06.3f", m, t - m * 60);
    return buf;
}

Font loadFont(const std::string& path, int size) {
    if (!std::filesystem::exists(path)) return GetFontDefault();
    Font f = LoadFontEx(path.c_str(), size, nullptr, 0);
    if (f.texture.id == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

}  // namespace

void Hud::init(const rr::Track& track, const std::string& assetsDir) {
    std::string dir = assetsDir + "/fonts/";
    regular_ = loadFont(dir + "DejaVuSans.ttf", 48);
    bold_ = loadFont(dir + "DejaVuSans-Bold.ttf", 64);
    mono_ = loadFont(dir + "DejaVuSansMono.ttf", 48);
    ownFonts_ = true;

    outline_.clear();
    minX_ = minY_ = 1e9f;
    maxX_ = maxY_ = -1e9f;
    for (int i = 0; i < track.size(); i += 4) {
        rr::Vec2 p = track.at(i).p;
        outline_.push_back(p);
        minX_ = std::min(minX_, p.x); maxX_ = std::max(maxX_, p.x);
        minY_ = std::min(minY_, p.y); maxY_ = std::max(maxY_, p.y);
    }
    trackWidth_ = track.at(0).halfWidth * 2;
}

void Hud::shutdown() {
    if (!ownFonts_) return;
    for (Font* f : {&regular_, &bold_, &mono_})
        if (f->texture.id != GetFontDefault().texture.id) UnloadFont(*f);
}

void Hud::text(const char* s, float x, float y, float size, Color c, bool bold, bool mono) {
    const Font& f = mono ? mono_ : (bold ? bold_ : regular_);
    DrawTextEx(f, s, {std::round(x), std::round(y)}, size, 0, c);
}

float Hud::width(const char* s, float size, bool bold, bool mono) {
    const Font& f = mono ? mono_ : (bold ? bold_ : regular_);
    return MeasureTextEx(f, s, size, 0).x;
}

void Hud::textRight(const char* s, float right, float y, float size, Color c, bool bold, bool mono) {
    text(s, right - width(s, size, bold, mono), y, size, c, bold, mono);
}

void Hud::panel(Rectangle r, float alpha) {
    DrawRectangleRounded(r, 8.0f / std::min(r.width, r.height), 8, Fade(Color{12, 14, 20, 255}, alpha));
}

void Hud::draw(const rr::Race& race, const HudState& st) {
    if (st.showHud) {
        if (st.qualifying) drawQualiTower(race, st);
        else drawTower(race, st);
        drawMinimap(race, st);
        drawCarPanel(race, st);
        const char* hint = "F1 help   Tab next car   L follow leader   C camera   M sound   Space pause   +/- speed   Esc menu";
        text(hint, 18, GetScreenHeight() - 30.0f, 16, Fade(kText, 0.75f));
        // camera and focus mode, top centre
        char buf[96];
        if (st.qualifying)
            std::snprintf(buf, sizeof buf, "QUALIFYING  RUN %d / %d   %s CAM   Enter: skip run   Shift+Enter: skip qualifying%s",
                          st.qualiRun, st.qualiRuns, camName(st.camera), st.muted ? "   SOUND OFF" : "");
        else
            std::snprintf(buf, sizeof buf, "%s CAM   %s%s", camName(st.camera), st.followLeader ? "FOLLOWING LEADER" : "CAR SELECTED",
                          st.muted ? "   SOUND OFF" : "");
        float w = width(buf, 15, true) + 28;
        float x = (GetScreenWidth() - w) / 2;
        panel({x, 12, w, 30});
        text(buf, x + 14, 18, 15, st.followLeader ? kAccent : kText, true);
    }
    if (st.paused && !race.isOver()) {
        const char* p = "PAUSED";
        float w = width(p, 44, true);
        text(p, (GetScreenWidth() - w) / 2, GetScreenHeight() * 0.2f, 44, kText, true);
    }
    if (race.isOver()) drawResults(race);
    if (st.showHelp) drawHelp();
}

static Color compoundColor(int compound) {
    return compound == RR_TIRE_SOFT ? Color{230, 50, 50, 255}
         : compound == RR_TIRE_HARD ? Color{235, 235, 240, 255}
                                    : Color{245, 200, 30, 255};
}

static const char* compoundName(int compound) {
    return compound == RR_TIRE_SOFT ? "SOFT" : compound == RR_TIRE_HARD ? "HARD" : "MEDIUM";
}

// Timing tower layout, shared by drawing and clicking.
static const float kTowerX = 16, kTowerW = 340, kTowerRowH = 28, kTowerTop = 112;

int Hud::towerCarAt(const rr::Race& race, const HudState& st, Vector2 p) const {
    if (!st.showHud || st.qualifying) return -1;
    if (p.x < kTowerX || p.x > kTowerX + kTowerW || p.y < kTowerTop - 3) return -1;
    const int row = (int)((p.y - (kTowerTop - 3)) / kTowerRowH);
    return row < (int)race.order().size() ? race.order()[row] : -1;
}

void Hud::drawTower(const rr::Race& race, const HudState& st) {
    const auto& cars = race.cars();
    const auto& order = race.order();
    const rr::Car& leader = cars[order[0]];
    const float x = kTowerX, w = kTowerW, rowH = kTowerRowH;
    const int hover = towerCarAt(race, st, GetMousePosition());
    float h = 104 + rowH * cars.size() + 8;
    panel({x, 16, w, h});

    text("RAYLIB RACERS", x + 16, 26, 15, kAccent, true);
    text(race.track().name().c_str(), x + 16, 46, 19, kText);
    char buf[96];
    std::snprintf(buf, sizeof buf, "LAP %d / %d", leader.currentLap(race.laps()), race.laps());
    text(buf, x + 16, 72, 26, kText, true);
    textRight(lapTime(race.time()).c_str(), x + w - 16, 76, 21, kText, false, true);
    if (st.paused) std::snprintf(buf, sizeof buf, "paused");
    else std::snprintf(buf, sizeof buf, "x%g", st.timeScale);
    textRight(buf, x + w - 16, 50, 16, kDim, false, true);

    float y = kTowerTop;
    for (size_t p = 0; p < order.size(); ++p) {
        int idx = order[p];
        const rr::Car& c = cars[idx];
        if (idx == st.focus) DrawRectangle((int)x + 6, (int)y - 3, (int)w - 12, (int)rowH - 2, Fade(WHITE, 0.12f));
        else if (idx == hover) DrawRectangle((int)x + 6, (int)y - 3, (int)w - 12, (int)rowH - 2, Fade(WHITE, 0.06f));
        if (c.blueCar >= 0) DrawRectangle((int)x + 6, (int)y - 3, (int)w - 12, (int)rowH - 2, Fade(kBlueFlag, 0.5f));
        std::snprintf(buf, sizeof buf, "%zu", p + 1);
        textRight(buf, x + 38, y, 19, kText, true);
        DrawRectangle((int)x + 46, (int)y + 1, 5, 19, teamColor(idx));
        {  // shrink long names to fit before the tyre column
            const float room = w - 112 - 12 - 60;
            float size = 19;
            while (size > 13 && width(c.name.c_str(), size) > room) size -= 1;
            text(c.name.c_str(), x + 60, y + (19 - size) * 0.5f, size, kText);
        }
        std::string gap;
        if (p == 0) gap = c.finished ? "FINISH" : "LEADER";
        else if (c.dnf) gap = "DNF";
        else if (c.lapsBehind > 0 && !c.finished) gap = "+" + std::to_string(c.lapsBehind) + (c.lapsBehind > 1 ? " LAPS" : " LAP");
        else if (c.gap < 0) gap = "-";
        else {
            std::snprintf(buf, sizeof buf, "+%.3f", c.gap);
            gap = buf;
        }
        if (c.finished && p > 0) gap = gap + " F";
        if (c.penalties > 0 && !c.dnf) {
            std::snprintf(buf, sizeof buf, "+%.0fs ", c.penaltyTime);
            gap = buf + gap;
        }
        Color gapCol = c.dnf ? Color{230, 90, 80, 255} : kDim;
        if (c.pitState != RR_PIT_NONE && !c.finished && !c.dnf) {
            gap = c.pitState == RR_PIT_SERVICE ? "IN BOX" : "PIT";
            gapCol = kAccent;
        }
        textRight(gap.c_str(), x + w - 16, y + 1, 17, gapCol, false, true);
        // tyre compound
        float cx = x + w - 112, cy = y + 11;
        DrawCircleV({cx, cy}, 8.5f, compoundColor(c.state.compound));
        DrawCircleV({cx, cy}, 5.5f, Color{25, 25, 30, 255});
        if (c.pitStops > 0) {
            std::snprintf(buf, sizeof buf, "%d", c.pitStops);
            textRight(buf, cx - 13, y + 2, 14, kDim, false, true);
        }
        y += rowH;
    }
}

void Hud::drawMinimap(const rr::Race& race, const HudState& st) {
    const float box = 270, pad = 18;
    float sx = maxX_ - minX_, sy = maxY_ - minY_;
    float scale = (box - 2 * pad) / std::max(sx, sy);
    float w = sx * scale + 2 * pad, h = sy * scale + 2 * pad;
    float x0 = GetScreenWidth() - w - 16, y0 = 16;
    panel({x0, y0, w, h});
    auto P = [&](rr::Vec2 p) { return Vector2{x0 + pad + (p.x - minX_) * scale, y0 + pad + (maxY_ - p.y) * scale}; };
    float thick = std::max(3.0f, trackWidth_ * scale);
    for (size_t i = 0; i < outline_.size(); ++i) {
        Vector2 a = P(outline_[i]), b = P(outline_[(i + 1) % outline_.size()]);
        DrawLineEx(a, b, thick + 2, Fade(BLACK, 0.5f));
    }
    for (size_t i = 0; i < outline_.size(); ++i) {
        Vector2 a = P(outline_[i]), b = P(outline_[(i + 1) % outline_.size()]);
        DrawLineEx(a, b, thick, Color{120, 125, 135, 255});
    }
    // start line tick
    rr::Vec2 s0 = race.track().at(0).p, n0 = race.track().at(0).n;
    DrawLineEx(P(s0 + n0 * 12.0f), P(s0 - n0 * 12.0f), 2, kText);

    const auto& cars = race.cars();
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < cars.size(); ++i) {
            bool focus = (int)i == st.focus;
            if ((pass == 1) != focus) continue;  // focused car on top
            Vector2 c = P(cars[i].state.pos);
            if (focus) DrawCircleV(c, 8, kText);
            DrawCircleV(c, focus ? 6 : 5, teamColor((int)i));
        }
}

void Hud::drawCarPanel(const rr::Race& race, const HudState& st) {
    const rr::Car& c = race.cars()[st.focus];
    const float w = 380, h = 252;
    const float x = GetScreenWidth() - w - 16, y = GetScreenHeight() - h - 16;
    panel({x, y, w, h});
    char buf[128];

    // flags above the panel: blue flag, penalties
    float fy = y - 38;
    if (c.blueCar >= 0) {
        DrawRectangleRounded({x, fy, w, 30}, 0.3f, 6, kBlueFlag);
        std::snprintf(buf, sizeof buf, "BLUE FLAG   let %s by", race.cars()[c.blueCar].name.c_str());
        text(buf, x + 14, fy + 6, 17, WHITE, true);
        fy -= 36;
    }
    if (c.penalties > 0) {
        panel({x, fy, w, 30}, 0.8f);
        std::snprintf(buf, sizeof buf, "PENALTY  +%.0f s  (%d)", c.penaltyTime, c.penalties);
        text(buf, x + 14, fy + 6, 17, Color{240, 110, 70, 255}, true);
    }

    DrawRectangle((int)x + 14, (int)y + 16, 5, 40, teamColor(st.focus));
    text(c.name.c_str(), x + 28, y + 12, 22, kText, true);
    const auto& lt = liveryTable();
    const std::string team = lt.empty() ? std::string() : lt[carLivery(st.focus)].team + "   ";
    std::snprintf(buf, sizeof buf, "%s%s%s%s", team.c_str(), c.robotName.c_str(), c.params.empty() ? "" : "  ", c.params.c_str());
    text(buf, x + 28, y + 38, 15, kDim);
    std::snprintf(buf, sizeof buf, "P%d", c.position);
    textRight(buf, x + w - 16, y + 12, 30, kAccent, true);

    // speed and gear
    float speed = rr::length(c.state.velWorld()) * 3.6f;
    std::snprintf(buf, sizeof buf, "%.0f", speed);
    textRight(buf, x + 150, y + 66, 54, kText, true);
    text("km/h", x + 156, y + 96, 15, kDim);
    std::snprintf(buf, sizeof buf, "%s", c.state.gear < 0 ? "R" : (c.state.gear == 0 ? "N" : std::to_string(c.state.gear).c_str()));
    text("GEAR", x + 212, y + 72, 13, kDim);
    text(buf, x + 214, y + 86, 30, kAccent, true);

    // rpm bar
    float rpmFrac = std::clamp(c.state.rpm / c.phys.maxRpm, 0.0f, 1.0f);
    Rectangle rb = {x + 16, y + 128, 230, 8};
    DrawRectangleRec(rb, Fade(WHITE, 0.12f));
    Color rpmCol = rpmFrac > 0.92f ? Color{240, 70, 60, 255} : (rpmFrac > 0.8f ? kAccent : Color{90, 200, 120, 255});
    DrawRectangleRec({rb.x, rb.y, rb.width * rpmFrac, rb.height}, rpmCol);

    // pedals and steering
    const RRControl& k = c.control;
    float px = x + 268, ph = 64, py = y + 66;
    DrawRectangle((int)px, (int)py, 14, (int)ph, Fade(WHITE, 0.12f));
    DrawRectangle((int)px, (int)(py + ph * (1 - k.accel)), 14, (int)(ph * k.accel), Color{80, 210, 110, 255});
    DrawRectangle((int)px + 20, (int)py, 14, (int)ph, Fade(WHITE, 0.12f));
    DrawRectangle((int)px + 20, (int)(py + ph * (1 - k.brake)), 14, (int)(ph * k.brake), Color{235, 70, 60, 255});
    float sxc = px + 70, sw = 34;
    DrawRectangle((int)(sxc - sw), (int)py + 26, (int)(2 * sw), 12, Fade(WHITE, 0.12f));
    float steer = std::clamp(k.steer, -1.0f, 1.0f);  // + = left: draw towards the left
    float s0 = sxc, s1 = sxc - steer * sw;
    DrawRectangle((int)std::min(s0, s1), (int)py + 26, (int)std::max(2.0f, std::fabs(s1 - s0)), 12, kAccent);
    text("THR  BRK   STEER", px - 2, py + ph + 4, 11, kDim);

    // laps
    float ly = y + 146;
    std::snprintf(buf, sizeof buf, "LAP %s", lapTime(race.time() - c.lapStart).c_str());
    if (c.finished) std::snprintf(buf, sizeof buf, "FINISHED %s", lapTime(c.finishTime).c_str());
    text(buf, x + 16, ly, 15, kText, false, true);
    std::snprintf(buf, sizeof buf, "LAST %s", lapTime(c.lapTimes.empty() ? 0 : c.lapTimes.back()).c_str());
    text(buf, x + 196, ly, 15, kDim, false, true);
    std::snprintf(buf, sizeof buf, "BEST %s", lapTime(c.bestLap).c_str());
    text(buf, x + 196, ly + 20, 15, kAccent, false, true);
    std::snprintf(buf, sizeof buf, "%s", k.status[0] ? k.status : "");
    text(buf, x + 16, ly + 20, 14, kDim);
    // consumables
    float cy = y + 196;
    text("FUEL", x + 16, cy, 13, kDim);
    float fuelFrac = std::clamp(c.state.fuel / c.phys.fuelCapacity, 0.0f, 1.0f);
    Rectangle fb = {x + 60, cy + 3, 120, 9};
    DrawRectangleRec(fb, Fade(WHITE, 0.12f));
    DrawRectangleRec({fb.x, fb.y, fb.width * fuelFrac, fb.height},
                     fuelFrac < 0.1f ? Color{240, 70, 60, 255} : Color{90, 170, 235, 255});
    std::snprintf(buf, sizeof buf, "%.1f L", c.state.fuel);
    text(buf, x + 188, cy - 1, 14, kText, false, true);
    if (c.pitStops > 0) {
        std::snprintf(buf, sizeof buf, "STOPS %d", c.pitStops);
        textRight(buf, x + w - 16, cy - 1, 14, kDim, false, true);
    }
    float ty = cy + 22;
    DrawCircleV({x + 22, ty + 7}, 7, compoundColor(c.state.compound));
    DrawCircleV({x + 22, ty + 7}, 4.5f, Color{25, 25, 30, 255});
    text(compoundName(c.state.compound), x + 34, ty, 13, kDim);
    for (int axle = 0; axle < 2; ++axle) {
        float wear = c.state.tireWear[axle];
        float bx = x + 100 + axle * 120;
        text(axle == 0 ? "F" : "R", bx, ty, 13, kDim);
        Rectangle wb = {bx + 14, ty + 3, 80, 9};
        DrawRectangleRec(wb, Fade(WHITE, 0.12f));
        Color wc = wear > 0.7f ? Color{240, 70, 60, 255} : (wear > 0.45f ? kAccent : Color{90, 200, 120, 255});
        DrawRectangleRec({wb.x, wb.y, wb.width * (1 - wear), wb.height}, wc);  // tyre life left
    }
    std::snprintf(buf, sizeof buf, "%d laps", c.lapsOnTires);
    textRight(buf, x + w - 16, ty - 1, 14, kDim, false, true);
    if (c.pitState == RR_PIT_SERVICE) {
        std::snprintf(buf, sizeof buf, "IN THE BOX  %.1f s", c.serviceLeft);
        textRight(buf, x + w - 16, y + 46, 14, kAccent, true);
    } else if (c.pitState != RR_PIT_NONE) {
        textRight("PIT LANE", x + w - 16, y + 46, 14, kAccent, true);
    }

    if (c.state.damage > 0) {
        std::snprintf(buf, sizeof buf, "dmg %.0f", c.state.damage);
        textRight(buf, x + w - 70, y + 20, 13, Color{230, 120, 100, 255});
    }
}

void Hud::drawHelp() {
    const char* lines[] = {
        "Tab / Right   next car",       "Left          previous car",       "1-9           focus car by position",
        "L             follow the leader", "C / Shift+C   next / prev camera",
        "F2-F8         follow, cinematic, TV, helicopter, top down, orbit, overview",
        "Mouse drag    orbit (orbit cam)", "Wheel         zoom (orbit, heli, top)",
        "Space         pause",          "+ / -         simulation speed",  "N             single step (paused)",
        "R             restart race",   "P             robot paths",       "S             range finders",
        "M             engine sound on / off", "Esc           race setup menu",
        "H             hide HUD",       "F12           screenshot",        "F1            close help"};
    const int n = sizeof(lines) / sizeof(lines[0]);
    float w = 760, h = 70 + n * 24.0f;
    float x = (GetScreenWidth() - w) / 2, y = (GetScreenHeight() - h) / 2;
    panel({x, y, w, h}, 0.85f);
    text("CONTROLS", x + 24, y + 20, 22, kAccent, true);
    for (int i = 0; i < n; ++i) text(lines[i], x + 24, y + 58 + i * 24.0f, 17, kText, false, true);
}

void Hud::drawResults(const rr::Race& race) {
    const auto& order = race.order();
    const float rowH = order.size() > 14 ? 27.0f : 30.0f;
    float w = 560, h = 90 + rowH * order.size();
    float x = (GetScreenWidth() - w) / 2, y = std::max(10.0f, std::min(GetScreenHeight() * 0.22f, (GetScreenHeight() - h) / 2));
    panel({x, y, w, h}, 0.82f);
    text("RESULTS", x + 24, y + 18, 26, kAccent, true);
    textRight("R to restart", x + w - 24, y + 24, 15, kDim);
    char buf[64];
    float ry = y + 62;
    const rr::Car& win = race.cars()[order[0]];
    for (size_t p = 0; p < order.size(); ++p) {
        const rr::Car& c = race.cars()[order[p]];
        std::snprintf(buf, sizeof buf, "%zu", p + 1);
        textRight(buf, x + 50, ry, 20, kText, true);
        DrawRectangle((int)x + 60, (int)ry + 2, 5, 20, teamColor(order[p]));
        text(c.name.c_str(), x + 76, ry, 20, kText);
        std::string t;
        if (c.finished) t = p == 0 ? lapTime(c.raceTime()) : "+" + lapTime(c.raceTime() - win.raceTime());
        else t = c.dnf ? "DNF" : "not finished";
        if (c.penalties > 0) {
            std::snprintf(buf, sizeof buf, "pen +%.0fs", c.penaltyTime);
            textRight(buf, x + w - 290, ry + 3, 14, Color{240, 110, 70, 255}, false, true);
        }
        textRight(t.c_str(), x + w - 170, ry + 1, 18, kText, false, true);
        std::snprintf(buf, sizeof buf, "best %s", lapTime(c.bestLap).c_str());
        textRight(buf, x + w - 24, ry + 2, 15, kDim, false, true);
        ry += rowH;
    }
}

void Hud::drawMenu(const MenuState& m, std::vector<MenuHit>& hits) {
    hits.clear();
    if (m.gridPage) {
        drawGridPage(m, hits);
        return;
    }
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(Color{8, 10, 16, 255}, 0.35f));
    const float w = 720, rowH = 64, h = 150 + MenuState::kRows * rowH + 50;
    const float x = (sw - w) / 2, y = std::max(20.0f, (sh - h) / 2);
    panel({x, y, w, h}, 0.88f);
    text("RAYLIB RACERS", x + 32, y + 26, 20, kAccent, true);
    text("Race setup", x + 32, y + 52, 36, kText, true);

    const TrackStats& ts = m.stats();
    char val[96], note[160];
    float ry = y + 120;
    for (int row = 0; row < MenuState::kRows; ++row, ry += rowH) {
        const bool sel = row == m.row;
        const Rectangle r = {x + 20, ry, w - 40, rowH - 8};
        hits.push_back({r.x, r.y, r.width, r.height, row, 0});
        if (row == MenuState::kStartRow) {
            Rectangle b = {x + 20, ry + 6, w - 40, rowH - 4};
            hits.back() = {b.x, b.y, b.width, b.height, row, 0};
            DrawRectangleRounded(b, 0.25f, 8, sel ? kAccent : Fade(kAccent, 0.75f));
            const char* s = "START RACE";
            text(s, b.x + (b.width - width(s, 26, true)) / 2, b.y + 15, 26, Color{20, 20, 24, 255}, true);
            continue;
        }
        if (sel) DrawRectangleRounded(r, 0.2f, 8, Fade(WHITE, 0.08f));
        static const char* labels[] = {"Track", "Race length", "Tyre life", "Cars", "Session", "Grid"};
        const char* label = labels[row];
        note[0] = 0;
        switch (row) {
            case 0:
                std::snprintf(val, sizeof val, "%s", ts.title.c_str());
                std::snprintf(note, sizeof note, "%.2f km, lap about %d:%02d", ts.length / 1000.0f,
                              (int)ts.lapTime / 60, (int)ts.lapTime % 60);
                break;
            case 1: {
                std::snprintf(val, sizeof val, "%d lap%s", m.laps, m.laps == 1 ? "" : "s");
                int mins = (int)std::lround(m.laps * ts.lapTime / 60.0f);
                float tank = m.lapsPerTank();
                int stops = (int)std::ceil(m.laps / tank - 1e-3f) - 1;
                std::snprintf(note, sizeof note, "about %d min.  Tank lasts %.0f laps: %s", std::max(1, mins), tank,
                              stops <= 0 ? "no fuel stop" : stops == 1 ? "1 fuel stop" : (std::to_string(stops) + " fuel stops").c_str());
                break;
            }
            case 2: {
                int life = MenuState::kTyreLives[m.tyreLife];
                if (life == 0) {
                    std::snprintf(val, sizeof val, "no wear");
                    std::snprintf(note, sizeof note, "tyres never wear out");
                } else {
                    std::snprintf(val, sizeof val, "%d laps", life);
                    // compounds wear at 1.7x (soft) and 0.6x (hard) the medium rate
                    std::snprintf(note, sizeof note, "soft %d, medium %d, hard %d laps before the grip falls away",
                                  std::max(1, (int)std::lround(life / 1.7f)), life, (int)std::lround(life / 0.6f));
                }
                break;
            }
            case 3:
                std::snprintf(val, sizeof val, "%d", m.cars);
                std::snprintf(note, sizeof note, "%d teams of two", (m.liveryCount + 1) / 2);
                break;
            case MenuState::kSessionRow:
                std::snprintf(val, sizeof val, "%s", m.weekend ? "Weekend" : "Race only");
                std::snprintf(note, sizeof note, "%s", m.weekend ? "qualifying sets the grid: each car runs alone, fastest lap wins pole"
                                                                 : "grid in the order of the Grid page");
                break;
            default:
                std::snprintf(val, sizeof val, "Edit");
                std::snprintf(note, sizeof note, "choose each car's livery and driving algorithm");
                break;
        }
        text(label, r.x + 16, r.y + 8, 21, sel ? kText : kDim, true);
        if (note[0]) text(note, r.x + 16, r.y + 34, 15, kDim);
        // value with arrows, right-aligned
        const float vr = r.x + r.width - 16;
        const float vw = std::max(150.0f, width(val, 22, true));
        const float ax = vr - vw - 44;
        text("<", ax, r.y + 12, 24, sel ? kAccent : kDim, true);
        text(">", vr - 12, r.y + 12, 24, sel ? kAccent : kDim, true);
        hits.push_back({ax - 10, r.y, 36, r.height, row, -1});
        hits.push_back({vr - 22, r.y, 36, r.height, row, 1});
        text(val, ax + 28 + (vw - width(val, 22, true)) / 2, r.y + 13, 22, kText, true);
    }
    const char* help = "Up/Down choose    Left/Right change (Shift: bigger steps)    Enter start    Esc quit";
    text(help, x + (w - width(help, 15)) / 2, y + h - 34, 15, kDim);
}

void Hud::drawGridPage(const MenuState& m, std::vector<MenuHit>& hits) {
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(Color{8, 10, 16, 255}, 0.45f));
    const int n = m.cars;
    const int perCol = n > 10 ? (n + 1) / 2 : n;
    const int cols = n > 10 ? 2 : 1;
    const float colW = 620, rowH = 34;
    const float w = cols * colW + 40, h = 130 + perCol * rowH + 70;
    const float x = (sw - w) / 2, y = std::max(10.0f, (sh - h) / 2);
    panel({x, y, w, h}, 0.9f);
    text("RACE SETUP", x + 28, y + 22, 18, kAccent, true);
    text("Grid", x + 28, y + 44, 32, kText, true);
    text("Livery", x + 28 + 52, y + 98, 14, kDim, true);
    text("Algorithm", x + 28 + 360, y + 98, 14, kDim, true);
    if (cols == 2) {
        text("Livery", x + 28 + colW + 52, y + 98, 14, kDim, true);
        text("Algorithm", x + 28 + colW + 360, y + 98, 14, kDim, true);
    }
    const auto& table = liveryTable();
    char buf[128];
    for (int car = 0; car < n; ++car) {
        const float cx = x + 20 + (car / perCol) * colW, cy = y + 120 + (car % perCol) * rowH;
        const bool selRow = car == m.gridRow;
        if (selRow) DrawRectangleRounded({cx, cy - 3, colW - 20, rowH - 2}, 0.3f, 6, Fade(WHITE, 0.08f));
        std::snprintf(buf, sizeof buf, "%d", car + 1);
        textRight(buf, cx + 30, cy + 3, 17, kDim, true);
        // livery: team colour chip, team and number
        const int slot = car < (int)m.carLivery.size() ? m.carLivery[car] : car;
        const bool haveTable = slot >= 0 && slot < (int)table.size();
        DrawRectangle((int)cx + 58, (int)cy + 3, 6, 20, haveTable ? table[slot].color : GRAY);
        if (haveTable) std::snprintf(buf, sizeof buf, "#%d  %s", table[slot].number, table[slot].team.c_str());
        else std::snprintf(buf, sizeof buf, "livery %d", slot + 1);
        const bool selL = selRow && m.gridCol == 0, selA = selRow && m.gridCol == 1;
        text("<", cx + 40, cy + 1, 20, selL ? kAccent : kDim, true);
        text(buf, cx + 72, cy + 3, 17, selL ? kText : Fade(kText, 0.85f), selL);
        text(">", cx + 316, cy + 1, 20, selL ? kAccent : kDim, true);
        hits.push_back({cx + 34, cy - 3, 26, rowH, 100 + car * 2, -1});
        hits.push_back({cx + 60, cy - 3, 250, rowH, 100 + car * 2, 0});
        hits.push_back({cx + 308, cy - 3, 26, rowH, 100 + car * 2, 1});
        // algorithm
        const int a = car < (int)m.carAlgo.size() ? m.carAlgo[car] : 0;
        const char* label = a < (int)m.algos.size() ? m.algos[a].label.c_str() : "?";
        text("<", cx + 346, cy + 1, 20, selA ? kAccent : kDim, true);
        text(label, cx + 370, cy + 3, 17, selA ? kText : Fade(kText, 0.85f), selA);
        text(">", cx + 580, cy + 1, 20, selA ? kAccent : kDim, true);
        hits.push_back({cx + 340, cy - 3, 26, rowH, 100 + car * 2 + 1, -1});
        hits.push_back({cx + 366, cy - 3, 206, rowH, 100 + car * 2 + 1, 0});
        hits.push_back({cx + 572, cy - 3, 26, rowH, 100 + car * 2 + 1, 1});
    }
    // Done button
    Rectangle b = {x + w - 180, y + h - 58, 150, 40};
    DrawRectangleRounded(b, 0.25f, 8, kAccent);
    text("DONE", b.x + (b.width - width("DONE", 20, true)) / 2, b.y + 9, 20, Color{20, 20, 24, 255}, true);
    hits.push_back({b.x, b.y, b.width, b.height, 99, 0});
    const char* help = "Up/Down car    Tab livery / algorithm    Left/Right change (a livery in use swaps)    Enter done";
    text(help, x + 28, y + h - 46, 15, kDim);
}

void Hud::drawQualiTower(const rr::Race& race, const HudState& st) {
    const float x = 16, w = 340, rowH = 28;
    const float h = 104 + rowH * st.quali.size() + 8;
    panel({x, 16, w, h});
    text("QUALIFYING", x + 16, 26, 15, kAccent, true);
    text(race.track().name().c_str(), x + 16, 46, 19, kText);
    char buf[96];
    std::snprintf(buf, sizeof buf, "RUN %d / %d", st.qualiRun, st.qualiRuns);
    text(buf, x + 16, 72, 26, kText, true);
    const rr::Car& c = race.cars()[0];
    std::snprintf(buf, sizeof buf, "LAP %d / %d", c.currentLap(race.laps()), race.laps());
    textRight(buf, x + w - 16, 78, 17, kDim, false, true);
    float y = 112;
    const float pole = st.quali.empty() ? 0 : st.quali[0].time;
    for (size_t p = 0; p < st.quali.size(); ++p, y += rowH) {
        const QualiLine& q = st.quali[p];
        if (q.running) DrawRectangle((int)x + 6, (int)y - 3, (int)w - 12, (int)rowH - 2, Fade(WHITE, 0.12f));
        std::snprintf(buf, sizeof buf, "%zu", p + 1);
        textRight(buf, x + 38, y, 19, q.time > 0 ? kText : kDim, true);
        DrawRectangle((int)x + 46, (int)y + 1, 5, 19, q.color);
        float size = 19;
        while (size > 13 && width(q.name.c_str(), size) > w - 60 - 100) size -= 1;
        text(q.name.c_str(), x + 60, y + (19 - size) * 0.5f, size, q.time > 0 || q.running ? kText : kDim);
        std::string t;
        if (q.running) t = "ON TRACK";
        else if (q.time <= 0) t = "-";
        else if (p == 0) t = lapTime(q.time);
        else {
            std::snprintf(buf, sizeof buf, "+%.3f", q.time - pole);
            t = buf;
        }
        textRight(t.c_str(), x + w - 16, y + 1, 17, q.running ? kAccent : kDim, false, true);
    }
}

void Hud::drawQualiResults(const HudState& st) {
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(Color{8, 10, 16, 255}, 0.45f));
    const int n = (int)st.quali.size();
    const int perCol = n > 10 ? (n + 1) / 2 : n, cols = n > 10 ? 2 : 1;
    const float colW = 460, rowH = 32;
    const float w = cols * colW + 40, h = 120 + perCol * rowH + 70;
    const float x = (sw - w) / 2, y = std::max(10.0f, (sh - h) / 2);
    panel({x, y, w, h}, 0.9f);
    text("QUALIFYING", x + 28, y + 22, 18, kAccent, true);
    text("Starting grid", x + 28, y + 44, 32, kText, true);
    char buf[96];
    const float pole = n ? st.quali[0].time : 0;
    for (int p = 0; p < n; ++p) {
        const QualiLine& q = st.quali[p];
        const float cx = x + 20 + (p / perCol) * colW, cy = y + 104 + (p % perCol) * rowH;
        std::snprintf(buf, sizeof buf, "%d", p + 1);
        textRight(buf, cx + 30, cy, 19, kText, true);
        DrawRectangle((int)cx + 40, (int)cy + 1, 5, 19, q.color);
        text(q.name.c_str(), cx + 54, cy, 18, kText);
        if (q.time <= 0) std::snprintf(buf, sizeof buf, "no time");
        else if (p == 0) std::snprintf(buf, sizeof buf, "%s", lapTime(q.time).c_str());
        else std::snprintf(buf, sizeof buf, "+%.3f", q.time - pole);
        textRight(buf, cx + colW - 40, cy + 1, 17, p == 0 ? kAccent : kDim, false, true);
    }
    const char* go = "Enter: start the race    Esc: back to setup";
    text(go, x + (w - width(go, 18, true)) / 2, y + h - 46, 18, kAccent, true);
}
