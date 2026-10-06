#include "hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

const Color kText = {238, 240, 245, 255};
const Color kDim = {160, 166, 180, 255};
const Color kAccent = {255, 196, 40, 255};

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
        drawTower(race, st);
        drawMinimap(race, st);
        drawCarPanel(race, st);
        const char* hint = "F1 help   Tab next car   L follow leader   C camera   Space pause   +/- speed";
        text(hint, 18, GetScreenHeight() - 30.0f, 16, Fade(kText, 0.75f));
        // camera and focus mode, top centre
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s CAM   %s", camName(st.camera), st.followLeader ? "FOLLOWING LEADER" : "CAR SELECTED");
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

void Hud::drawTower(const rr::Race& race, const HudState& st) {
    const auto& cars = race.cars();
    const auto& order = race.order();
    const rr::Car& leader = cars[order[0]];
    const float x = 16, w = 340, rowH = 28;
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

    float y = 112;
    for (size_t p = 0; p < order.size(); ++p) {
        int idx = order[p];
        const rr::Car& c = cars[idx];
        if (idx == st.focus) DrawRectangle((int)x + 6, (int)y - 3, (int)w - 12, (int)rowH - 2, Fade(WHITE, 0.12f));
        std::snprintf(buf, sizeof buf, "%zu", p + 1);
        textRight(buf, x + 38, y, 19, kText, true);
        DrawRectangle((int)x + 46, (int)y + 1, 5, 19, teamColor(idx));
        text(c.name.c_str(), x + 60, y, 19, kText);
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

    DrawRectangle((int)x + 14, (int)y + 16, 5, 40, teamColor(st.focus));
    text(c.name.c_str(), x + 28, y + 12, 22, kText, true);
    std::snprintf(buf, sizeof buf, "%s%s%s", c.robotName.c_str(), c.params.empty() ? "" : "  ", c.params.c_str());
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
    float w = 560, h = 90 + 30.0f * order.size();
    float x = (GetScreenWidth() - w) / 2, y = GetScreenHeight() * 0.22f;
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
        if (c.finished) t = p == 0 ? lapTime(c.finishTime) : "+" + lapTime(c.finishTime - win.finishTime);
        else t = c.dnf ? "DNF" : "not finished";
        textRight(t.c_str(), x + w - 170, ry + 1, 18, kText, false, true);
        std::snprintf(buf, sizeof buf, "best %s", lapTime(c.bestLap).c_str());
        textRight(buf, x + w - 24, ry + 2, 15, kDim, false, true);
        ry += 30;
    }
}
