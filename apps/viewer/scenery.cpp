// Scenery around the track: grandstands, buildings, trees and landmarks in the style of
// the track's place (the .trk file's `scenery` theme). Everything is built once from
// simple shapes and placed from the track's seed, so a track always looks the same.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <random>
#include <unordered_map>

#include "mini_json.hpp"

#include "renderer.hpp"
#include "raymath.h"
#include "rlgl.h"

using rr::Vec2;

namespace {

Vector3 W(Vec2 p, float h = 0) { return {p.x, h, -p.y}; }
float yawOf(Vec2 t) { return std::atan2(t.y, t.x); }

unsigned hashi(int x, int y, int seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float hash01(int x, int y, int seed) { return (hashi(x, y, seed) & 0xffffff) / 16777215.0f; }
// Smooth value noise, 0..1, features about `cell` metres across: forests and clearings.
float noise(float x, float y, float cell, int seed) {
    x /= cell, y /= cell;
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
    const float a = hash01(xi, yi, seed), b = hash01(xi + 1, yi, seed), c = hash01(xi, yi + 1, seed),
                d = hash01(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

Color mix(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

}  // namespace

void Renderer::buildScenery(const rr::Track& tr, unsigned seed) {
    props_.clear();
    tvSpots_.clear();
    treeTiles_.clear();
    std::mt19937 rng(seed * 7919u + 17u);
    std::uniform_real_distribution<float> U(0, 1);
    auto R = [&](float a, float b) { return a + (b - a) * U(rng); };
    const std::string& th = theme_;

    // ---- helpers
    auto add = [&](PropKind k, Vector3 pos, Vector3 size, float yaw, Color c, float tilt = 0) {
        Prop p;
        p.kind = k;
        p.pos = pos;
        p.size = size;
        p.yaw = yaw;
        p.tilt = tilt;
        p.color = c;
        p.radius = std::max({size.x, size.y, size.z}) * 0.75f + 1.0f;
        props_.push_back(p);
    };
    struct Foot { Vec2 c; float r; };
    std::vector<Foot> feet;  // ground taken by buildings, stands and water
    const float edge = tr.runoff();
    auto clearOfTrack = [&](Vec2 p, float margin) {
        const rr::TrackLoc loc = tr.locateGlobal(p);
        return std::fabs(loc.lateral) > loc.halfWidth + edge + margin;
    };
    auto free = [&](Vec2 p, float margin, float own = 0) {
        if (!clearOfTrack(p, margin + own)) return false;
        for (const Foot& f : feet)
            if (rr::length(p - f.c) < f.r + own) return false;
        return true;
    };
    auto pitSide = tr.hasPit() ? tr.pit().side : -1;
    auto sample = [&](float s) -> const rr::TrackSample& { return tr.at(tr.indexAt(s)); };
    // the side away from the corner's centre (+1 left of the direction of travel)
    auto outside = [&](float s) { return sample(s).curvature > 0 ? -1.0f : 1.0f; };
    const Vec2 ctr = {trackCenter_.x, -trackCenter_.z};
    const float half = trackExtent_ * 0.5f;
    auto randomPoint = [&](float extra) {
        return Vec2{ctr.x + R(-1, 1) * (half + extra), ctr.y + R(-1, 1) * (half + extra)};
    };

    // ---- building blocks
    // A grandstand along the track at s, on `side`, `len` long: rows of seats stepping up and
    // back, a roof on posts. Returns false where it does not fit.
    auto grandstand = [&](float s, float side, float len, int rows, Color seatA, Color seatB, Color roof, float lift = 0) {
        const auto& sm = sample(s);
        const float base = sm.halfWidth + edge + 4.0f;
        const float depth = rows * 1.6f;
        for (float a = -len * 0.5f; a <= len * 0.5f; a += 10) {
            const Vec2 q = tr.pointAt(s + a, side * (sample(s + a).halfWidth + edge + 3.0f));
            if (!clearOfTrack(q, 0.5f)) return false;
            for (const Foot& f : feet)
                if (rr::length(q - f.c) < f.r) return false;
        }
        const float yaw = yawOf(sm.t);
        const Vec2 n = sm.n * side;
        if (lift > 0)  // a bank or concrete base under the stand
            add(P_BOX, W(sm.p + n * (base + depth * 0.5f), lift * 0.5f), {len + 4, lift, depth + 3}, yaw, {150, 150, 145, 255});
        for (int k = 0; k < rows; ++k) {
            const float h = 0.9f * (k + 1);
            add(P_BOX, W(sm.p + n * (base + 1.6f * k + 0.8f), lift + h * 0.5f), {len, h, 1.6f}, yaw, k % 2 ? seatA : seatB);
        }
        const float top = lift + 0.9f * rows + 3.5f;
        add(P_BOX, W(sm.p + n * (base + depth * 0.5f), top), {len + 6, 0.4f, depth + 4}, yaw, roof, side * -0.08f);
        for (int k = 0; k <= 4; ++k) {
            const Vec2 q = sm.p + sm.t * (-len * 0.5f + len * k / 4.0f) + n * (base + depth + 1.0f);
            add(P_BOX, W(q, top * 0.5f), {0.5f, top, 0.5f}, yaw, {70, 72, 80, 255});
        }
        for (float a = -len * 0.5f; a <= len * 0.5f; a += 12)
            feet.push_back({sm.p + sm.t * a + n * (base + depth * 0.5f), depth * 0.5f + 8});
        return true;
    };
    // A building: walls and a roof (0 flat, 1 hipped, 2 steep hipped).
    auto building = [&](Vec2 c, float yaw, float w, float d, float h, Color wall, int roof, Color roofColor) {
        add(P_BOX, W(c, h * 0.5f), {w, h, d}, yaw, wall);
        if (roof == 0) add(P_BOX, W(c, h + 0.25f), {w + 0.6f, 0.5f, d + 0.6f}, yaw, roofColor);
        else add(P_PYRAMID, W(c, h), {w * 0.55f, roof == 2 ? std::min(w, d) * 0.75f : std::min(w, d) * 0.35f, d * 0.55f},
                 yaw, roofColor);
        feet.push_back({c, std::max(w, d) * 0.6f + 4});
    };
    auto hangar = [&](Vec2 c, float yaw, float len, float wid, float h, Color wall, Color roof) {
        add(P_BOX, W(c, h * 0.5f), {len, h, wid}, yaw, wall);
        add(P_ROOF, W(c, h), {wid * 0.5f, len, wid * 0.32f}, yaw, roof);
        feet.push_back({c, std::max(len, wid) * 0.6f + 4});
    };
    auto billboard = [&](float s, float side, Color c) {
        const auto& sm = sample(s);
        const Vec2 q = sm.p + sm.n * (side * (sm.halfWidth + edge + 2.5f));
        if (!free(q, 1.5f)) return;
        const float yaw = yawOf(sm.t);
        add(P_BOX, W(q, 2.6f), {9.0f, 2.0f, 0.3f}, yaw, c);
        add(P_BOX, W(q + sm.t * 3.5f, 1.0f), {0.25f, 2.0f, 0.25f}, yaw, {60, 60, 66, 255});
        add(P_BOX, W(q - sm.t * 3.5f, 1.0f), {0.25f, 2.0f, 0.25f}, yaw, {60, 60, 66, 255});
    };
    // Trees, standing at `base`, scale 1 about 10 m tall. Each is a few instanced parts.
    enum TreeKind { CONIFER, BROADLEAF, PALM, BUSH };
    const float tileSize = 300;
    std::unordered_map<long long, int> tileOf;
    auto part = [&](int k, Vector3 at, const Matrix& M, Color c) {
        const long long key = ((long long)std::floor(at.x / tileSize) << 32) ^ (unsigned)(int)std::floor(at.z / tileSize);
        auto it = tileOf.find(key);
        if (it == tileOf.end()) {
            it = tileOf.emplace(key, (int)treeTiles_.size()).first;
            treeTiles_.emplace_back();
            treeTiles_.back().parts.resize(treeSlots());
        }
        Matrix m = M;
        m.m3 = c.r / 255.0f, m.m7 = c.g / 255.0f, m.m11 = c.b / 255.0f;
        treeTiles_[it->second].parts[k].push_back(m);
    };
    auto treeAt = [&](TreeKind k, Vector3 base, float s, Color leaf) {
        const float yaw = R(0, 2 * PI);
        const Matrix T = MatrixTranslate(base.x, base.y, base.z);
        auto at = [&](float x, float y, float z, float sx, float sy, float sz) {
            return MatrixMultiply(MatrixScale(sx, sy, sz), MatrixMultiply(MatrixTranslate(x, y, z), T));
        };
        // a tree model when there are some: textured, the tint only shades it a little
        auto pick = [&](const char* kind) -> const TreeModel* {
            int n = 0;
            for (const TreeModel& m : treeModels_) n += m.kind == kind;
            if (n == 0) return nullptr;
            int i = (int)R(0, (float)n - 0.001f);
            for (const TreeModel& m : treeModels_)
                if (m.kind == kind && i-- == 0) return &m;
            return nullptr;
        };
        const TreeModel* model = nullptr;
        float height = 0;
        if (k == CONIFER) model = pick("tree"), height = R(12, 15) * s;
        if (k == BROADLEAF) model = pick("tree"), height = R(9, 12) * s;
        if (k == BUSH) model = pick("bush"), height = R(1.6f, 2.4f) * s;
        if (model) {
            const Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(height, height, height), MatrixRotateY(yaw)), T);
            const float v = R(0.85f, 1.05f);
            const Color tint = mix({(unsigned char)(235 * v), (unsigned char)(240 * v), (unsigned char)(235 * v), 255},
                                   {(unsigned char)std::min(255, leaf.r * 3), (unsigned char)std::min(255, leaf.g * 2),
                                    (unsigned char)std::min(255, leaf.b * 3), 255}, 0.2f);
            for (int slot : model->slots) part(slot, base, M, tint);
            return;
        }
        const Color bark = mix({92, 66, 45, 255}, {70, 56, 44, 255}, R(0, 1));
        switch (k) {
            case CONIFER: {
                const float w = R(0.8f, 1.1f);  // some slim, some broad
                part(TP_TRUNK, base, at(0, 0, 0, 0.28f * s, 2.4f * s, 0.28f * s), bark);
                part(TP_CONE, base, at(0, 1.6f * s, 0, 2.4f * s * w, 5.0f * s, 2.4f * s * w), leaf);
                part(TP_CONE, base, at(0, 3.9f * s, 0, 1.7f * s * w, 3.8f * s, 1.7f * s * w), leaf);
                break;
            }
            case BROADLEAF: {
                part(TP_TRUNK, base, at(0, 0, 0, 0.32f * s, 3.4f * s, 0.32f * s), bark);
                part(TP_BLOB, base, at(0, 5.0f * s, 0, 2.9f * s, 2.5f * s, 2.9f * s), leaf);
                const float ox = std::cos(yaw) * 1.6f * s, oz = std::sin(yaw) * 1.6f * s;
                const Color c2 = {(unsigned char)(leaf.r * 0.85f), (unsigned char)(leaf.g * 0.9f), (unsigned char)(leaf.b * 0.85f), 255};
                part(TP_BLOB2, base, at(ox, 4.0f * s, oz, 2.0f * s, 1.8f * s, 2.0f * s), c2);
                break;
            }
            case PALM: {
                // a leaning trunk and a crown of drooping fronds
                const float lean = 0.15f;
                const Matrix YT = MatrixMultiply(MatrixRotateY(yaw), T);
                part(TP_TRUNK, base, MatrixMultiply(MatrixMultiply(MatrixScale(0.25f * s, 8.0f * s, 0.25f * s), MatrixRotateZ(lean)), YT),
                     {120, 96, 70, 255});
                const Vector3 top = Vector3Transform({0, 8.0f * s, 0}, MatrixMultiply(MatrixRotateZ(lean), YT));
                for (int j = 0; j < 6; ++j) {
                    Matrix M = MatrixMultiply(MatrixScale(3.6f * s, 0.12f * s, 0.7f * s), MatrixTranslate(1.6f * s, 0, 0));
                    M = MatrixMultiply(M, MatrixRotateZ(-0.45f));
                    M = MatrixMultiply(M, MatrixRotateY(yaw + j * PI / 3));
                    part(TP_FROND, base, MatrixMultiply(M, MatrixTranslate(top.x, top.y, top.z)), leaf);
                }
                break;
            }
            case BUSH:
                part(TP_BLOB, base, at(0, 0.6f * s, 0, 1.5f * s, 1.0f * s, 1.5f * s), leaf);
                break;
        }
    };
    auto tree = [&](TreeKind k, Vec2 p, float scale, Color leaf) { treeAt(k, W(p), scale, leaf); };
    auto mound = [&](Vec2 c, float rx, float h, float rz, float yaw, Color col) {
        // the cap of a sunken, flattened sphere: a gentle hill `h` high, about 0.66 rx across
        add(P_MOUND, W(c, -h * 3.0f), {rx, h * 4.0f, rz}, yaw, col);
        feet.push_back({c, std::max(rx, rz) * 0.85f});
    };
    // Trees where noise says forest: a jittered grid `spacing` apart over the land around the
    // track, kept where the noise (features about `cell` across) is under `density` (0..1).
    auto woods = [&](float spacing, float density, float cell, float margin, float extra,
                     const std::function<void(Vec2, float)>& plant) {
        const int salt = (int)(cell * 7 + spacing * 13);
        for (float y = ctr.y - half - extra; y < ctr.y + half + extra; y += spacing)
            for (float x = ctr.x - half - extra; x < ctr.x + half + extra; x += spacing) {
                const Vec2 p = {x + R(-0.45f, 0.45f) * spacing, y + R(-0.45f, 0.45f) * spacing};
                const float n = 0.75f * noise(p.x, p.y, cell, (int)seed + salt) + 0.25f * noise(p.x, p.y, cell * 0.25f, (int)seed + salt + 1);
                const float r = R(0, 1);
                if (n > density) continue;
                if (!free(p, margin, 2.0f)) continue;
                plant(p, r);
            }
    };
    // A spot of open land at least `r` from the track and other things, searched from s outwards.
    auto findSpot = [&](float s, float side, float dist, float r, Vec2& out) {
        for (int k = 0; k < 40; ++k) {
            const float ss = s + (k % 2 ? 1 : -1) * 15.0f * (k / 2);
            const auto& sm = sample(ss);
            const Vec2 q = sm.p + sm.n * (side * (sm.halfWidth + edge + dist + r));
            if (free(q, dist, r)) {
                out = q;
                return true;
            }
        }
        return false;
    };

    // ---- the start line gantry, the pit building's paddock, TV spots
    {
        const auto& s0 = tr.at(0);
        const float yaw0 = yawOf(s0.t);
        const float postOff = s0.halfWidth + edge + 1.2f;
        const Color gantry = {55, 58, 66, 255};
        for (float side : {1.0f, -1.0f}) add(P_BOX, W(s0.p + s0.n * (side * postOff), 3.75f), {0.6f, 7.5f, 0.6f}, yaw0, gantry);
        add(P_BOX, W(s0.p, 7.2f), {0.9f, 1.4f, 2 * postOff + 0.6f}, yaw0, gantry);
        for (float side : {1.0f, -1.0f}) feet.push_back({s0.p + s0.n * (side * postOff), 3});
    }
    if (tr.hasPit()) {
        // garages and the paddock behind them: keep everything else away
        const RRPitInfo& p = tr.pit();
        const float len = std::fmod(p.lane_end_s - p.lane_start_s + tr.length(), tr.length());
        for (float a = -40; a <= len + 40; a += 15) {
            const auto& sm = sample(p.lane_start_s + a);
            feet.push_back({sm.p + sm.n * (p.side * (sm.halfWidth + 22.0f)), 22});
        }
        // paddock: team motorhomes in a row behind the garages
        const float mid = p.lane_start_s + len * 0.5f;
        const auto& sm = sample(mid);
        const float yaw = yawOf(sm.t);
        const int n = std::clamp((int)(len / 40), 2, 10);
        for (int k = 0; k < n; ++k) {
            const float a = -len * 0.4f + len * 0.8f * (k + 0.5f) / n;
            const Vec2 q = sm.p + sm.t * a + sm.n * (p.side * (sm.halfWidth + 48.0f));
            if (!clearOfTrack(q, 20)) continue;
            static const Color team[] = {{200, 30, 30, 255}, {240, 130, 20, 255}, {30, 160, 150, 255}, {40, 60, 150, 255},
                                         {20, 110, 70, 255}, {230, 90, 160, 255}, {235, 235, 240, 255}, {40, 40, 44, 255},
                                         {240, 200, 30, 255}, {140, 145, 155, 255}};
            add(P_BOX, W(q, 3.2f), {14, 6.4f, 9}, yaw, {228, 230, 234, 255});
            add(P_BOX, W(q, 6.6f), {14.4f, 0.5f, 9.4f}, yaw, team[k % 10]);
        }
    }
    for (float s = 0; s < tr.length(); s += 250) {
        const auto& sm = sample(s);
        const float side = outside(s);
        const Vec2 q = sm.p + sm.n * (side * (sm.halfWidth + edge + 8.0f));
        tvSpots_.push_back(W(q, 6.0f));
        feet.push_back({q, 6});  // keep hills and buildings off the cameras
    }

    // ---- grandstands: the main straight (away from the pits), then the outside of the slow corners
    const Color seatBlue = {40, 62, 130, 255}, seatGrey = {190, 192, 198, 255}, roofWhite = {225, 225, 228, 255};
    Color seatA = seatBlue, seatB = seatGrey, roofC = roofWhite;
    if (th == "parkland") seatA = {170, 30, 35, 255};
    if (th == "forest") seatA = {200, 150, 30, 255};
    if (th == "tropical") seatA = {230, 190, 40, 255}, seatB = {40, 40, 46, 255};
    if (th == "dunes") seatA = {235, 110, 20, 255};
    if (th == "hills") seatA = {40, 120, 60, 255};
    {
        // the longest straight-ish stretch through the line
        float bestLen = 0, bestS = 0;
        for (float s = -300; s < 200; s += 10) {
            float len = 0;
            while (len < 500 && std::fabs(sample(s + len).curvature) < 1.0f / 600.0f) len += 5;
            if (len > bestLen) bestLen = len, bestS = s;
        }
        if (bestLen >= 120)
            grandstand(bestS + bestLen * 0.5f, -pitSide, std::min(240.0f, bestLen - 40), 8, seatA, seatB, roofC);
    }
    {
        // corners: the tightest ones get stands on the outside, some on banks
        std::vector<std::pair<float, float>> bends;  // curvature, s
        for (float s = 0; s < tr.length(); s += 20) {
            const float k = std::fabs(sample(s).curvature);
            if (k > 1.0f / 90.0f) bends.push_back({k, s});
        }
        std::sort(bends.rbegin(), bends.rend());
        std::vector<float> used;
        const int want = th == "airfield" ? 6 : th == "forest" || th == "woodland" ? 3 : 4;
        int placed = 0;
        for (const auto& b : bends) {
            if (placed >= want) break;
            bool near = false;
            for (float u : used) near = near || std::fabs(std::remainder(b.second - u, tr.length())) < 300;
            if (near) continue;
            used.push_back(b.second);
            const float lift = th == "hills" || th == "dunes" || th == "woodland" ? 3.0f : 0.0f;
            // set back from the apex: stands look at the braking zone
            if (grandstand(b.second - 60, outside(b.second - 60), 90, 6, seatA, seatB, roofC, lift)) ++placed;
        }
    }
    // advertising boards along the straights
    {
        const Color ads[] = {{220, 40, 40, 255}, {250, 200, 30, 255}, {30, 90, 200, 255}, {240, 240, 240, 255},
                             {20, 150, 90, 255}, {245, 120, 20, 255}};
        int k = 0;
        for (float s = 40; s < tr.length(); s += 70)
            if (std::fabs(sample(s).curvature) < 1.0f / 400.0f) billboard(s, -pitSide, ads[k++ % 6]);
    }

    // ---- the theme: landmarks, buildings, then the plants
    const Color leafGreen = {52, 104, 46, 255}, leafDark = {34, 78, 40, 255}, pine = {36, 84, 50, 255};
    if (th == "parkland") {
        // Monza: a royal park. The old banked oval in the woods, a villa, tall broadleaf trees.
        Vec2 spot;
        if (findSpot(tr.length() * 0.62f, -outside(tr.length() * 0.62f), 70, 90, spot)) {
            const float r = 85, yaw = R(0, PI);
            for (int k = 0; k < 18; ++k) {
                const float a = yaw + k * 0.12f;
                const Vec2 q = spot + Vec2{std::cos(a), std::sin(a)} * r;
                add(P_BOX, W(q, 3.0f), {11.0f, 0.6f, 12.0f}, a + PI / 2, {170, 168, 160, 255}, 0.55f);
                add(P_BOX, W(q + Vec2{std::cos(a), std::sin(a)} * 6.5f, 4.5f), {11.0f, 9.0f, 0.8f}, a + PI / 2, {150, 148, 142, 255});
            }
            feet.push_back({spot, r + 20});
        }
        if (findSpot(tr.length() * 0.3f, outside(tr.length() * 0.3f), 90, 50, spot)) {
            const float yaw = R(0, PI);
            building(spot, yaw, 70, 18, 14, {226, 206, 160, 255}, 1, {150, 70, 50, 255});
            building(spot + Vec2{std::cos(yaw + PI / 2), std::sin(yaw + PI / 2)} * 26, yaw, 22, 30, 16, {226, 206, 160, 255}, 1,
                     {150, 70, 50, 255});
        }
        woods(11, 0.5f, 320, 10, 350, [&](Vec2 p, float r) {
            tree(r < 0.85f ? BROADLEAF : CONIFER, p, R(1.0f, 1.6f), mix(leafGreen, leafDark, R(0, 1)));
        });
    } else if (th == "forest") {
        // Spa: the Ardennes. Dark pine forest over hills, wooden chalets.
        for (int k = 0; k < 14; ++k) {
            const Vec2 p = randomPoint(450);
            if (!free(p, 140, 120)) continue;
            mound(p, R(210, 390), R(40, 80), R(210, 390), R(0, PI), {44, 86, 46, 255});
        }
        for (int k = 0; k < 8; ++k) {
            const Vec2 p = randomPoint(100);
            if (!free(p, 25, 10)) continue;
            building(p, R(0, PI), R(12, 18), R(9, 12), R(4, 6), {120, 86, 60, 255}, 2, {60, 50, 46, 255});
        }
        woods(9, 0.62f, 280, 9, 400, [&](Vec2 p, float r) {
            tree(r < 0.8f ? CONIFER : BROADLEAF, p, R(1.1f, 1.9f), r < 0.8f ? mix(pine, leafDark, R(0, 1)) : leafGreen);
        });
        // trees on the hills too
        for (const Prop& m : std::vector<Prop>(props_)) {
            if (m.kind != P_MOUND) continue;
            const int n = (int)(m.size.x * m.size.z * 0.4f / 110.0f);  // as dense as the woods
            const Matrix rot = MatrixRotateY(m.yaw);
            for (int k = 0; k < n; ++k) {
                const float a = R(0, 2 * PI), d = 0.62f * std::sqrt(R(0, 1));
                const float x = std::cos(a) * d, z = std::sin(a) * d;
                const float h = m.pos.y + m.size.y * std::sqrt(std::max(0.0f, 1 - d * d)) - 0.5f;
                const Vector3 off = Vector3Transform({x * m.size.x, 0, z * m.size.z}, rot);
                treeAt(CONIFER, {m.pos.x + off.x, h, m.pos.z + off.z}, R(1.3f, 2.0f), mix(pine, leafDark, R(0, 1)));
            }
        }
    } else if (th == "airfield") {
        // Silverstone: a wartime airfield. Old runways across the infield, hangars, a long
        // modern pit building, flat open land with hedges and a few clumps of trees.
        for (int k = 0; k < 3; ++k) {
            const float yaw = R(0, PI), len = R(600, 1100);
            const Vec2 c = ctr + Vec2{R(-0.3f, 0.3f), R(-0.3f, 0.3f)} * half;
            const Vec2 d = {std::cos(yaw), std::sin(yaw)};
            // strips of runway where they do not cross the track
            for (float a = -len * 0.5f; a < len * 0.5f; a += 30) {
                const Vec2 q = c + d * a;
                if (!clearOfTrack(q, 12)) continue;
                add(P_BOX, W(q, 0.03f), {30, 0.04f, 42}, yaw, {108, 108, 104, 255});
            }
        }
        for (int k = 0; k < 5; ++k) {
            const Vec2 p = randomPoint(80);
            if (!free(p, 30, 30)) continue;
            hangar(p, R(0, PI), R(40, 60), R(28, 36), R(8, 11), {150, 152, 150, 255}, {120, 124, 122, 255});
        }
        // the wing: a long pit building with a wave of roof panels
        if (tr.hasPit()) {
            const RRPitInfo& p = tr.pit();
            const float len = std::fmod(p.lane_end_s - p.lane_start_s + tr.length(), tr.length());
            const auto& sm = sample(p.lane_start_s + len * 0.5f);
            const float yaw = yawOf(sm.t);
            const float n = std::max(4.0f, (len - 40) / 12);
            for (int k = 0; k < (int)n; ++k) {
                const float a = -(len - 40) * 0.5f + 12 * k;
                const Vec2 q = sm.p + sm.t * a + sm.n * (p.side * (sm.halfWidth + 26.0f));
                add(P_BOX, W(q, 10 + 2.5f * std::sin(k * 0.45f)), {12.4f, 0.6f, 22.0f}, yaw, {235, 236, 240, 255},
                    0.12f * std::cos(k * 0.45f));
                add(P_BOX, W(q, 4.5f), {12.0f, 9.0f, 16.0f}, yaw, {200, 205, 212, 255});
            }
        }
        // hedgerows
        for (int k = 0; k < 10; ++k) {
            const Vec2 c = randomPoint(250);
            const float yaw = R(0, PI);
            const Vec2 d = {std::cos(yaw), std::sin(yaw)};
            for (float a = 0; a < R(80, 200); a += 3.5f) {
                const Vec2 q = c + d * a;
                if (!free(q, 6, 1)) break;
                tree(BUSH, q, R(1.1f, 1.5f), mix(leafGreen, leafDark, R(0, 1)));
            }
        }
        woods(13, 0.3f, 200, 14, 350, [&](Vec2 p, float) { tree(BROADLEAF, p, R(1.0f, 1.5f), mix(leafGreen, leafDark, R(0, 1))); });
    } else if (th == "hills") {
        // Hungaroring: a bowl in rolling hills, dry summer grass, bushes and villages.
        for (int k = 0; k < 18; ++k) {
            const Vec2 p = randomPoint(500);
            if (!free(p, 120, 100)) continue;
            mound(p, R(180, 360), R(25, 55), R(180, 360), R(0, PI), {110, 124, 58, 255});
        }
        for (int k = 0; k < 14; ++k) {
            const Vec2 p = randomPoint(150);
            if (!free(p, 30, 10)) continue;
            building(p, R(0, PI), R(10, 14), R(8, 10), R(4, 5.5f), {236, 226, 200, 255}, 1, {176, 74, 46, 255});
        }
        woods(12, 0.42f, 200, 9, 350, [&](Vec2 p, float r) {
            if (r < 0.55f) tree(BUSH, p, R(1.2f, 2.0f), mix({86, 112, 50, 255}, leafDark, R(0, 1)));
            else tree(BROADLEAF, p, R(0.9f, 1.4f), mix(leafGreen, {96, 116, 52, 255}, R(0, 1)));
        });
    } else if (th == "dunes") {
        // Zandvoort: sand dunes by the North Sea, marram grass, a few pines, beach houses.
        const float seaX = ctr.x - half - 260;
        add(P_BOX, W({seaX - 1500, ctr.y}, 0.05f), {3000, 0.1f, 6000}, 0, {46, 96, 140, 255});
        add(P_BOX, W({seaX + 60, ctr.y}, 0.06f), {120, 0.1f, 6000}, 0, {222, 206, 160, 255});  // the beach
        for (int k = 0; k < 8; ++k) {
            const Vec2 p = {seaX + 110 + R(-20, 20), ctr.y + R(-1, 1) * half};
            building(p, 0, R(8, 12), R(6, 8), R(3.5f, 5), k % 2 ? Color{240, 240, 236, 255} : Color{70, 130, 170, 255}, 1,
                     {80, 80, 86, 255});
        }
        for (int k = 0; k < 40; ++k) {
            const Vec2 p = randomPoint(260);
            if (!free(p, 30, 30) || p.x < seaX + 150) continue;
            mound(p, R(60, 160), R(8, 22), R(60, 160), R(0, PI), mix({140, 132, 90, 255}, {118, 116, 76, 255}, R(0, 1)));
        }
        woods(10, 0.55f, 140, 6, 300, [&](Vec2 p, float r) {
            if (p.x < seaX + 150) return;
            if (r < 0.85f) tree(BUSH, p, R(0.6f, 1.0f), mix({150, 150, 80, 255}, {110, 124, 60, 255}, R(0, 1)));
            else tree(CONIFER, p, R(0.9f, 1.3f), pine);
        });
    } else if (th == "tropical") {
        // Sepang: palms and rainforest, a stand with leaf-shaped roofs, a city skyline far off.
        if (tr.hasPit()) {
            const auto& sm = sample(0);
            const Vec2 n = sm.n * (float)-pitSide;
            for (int k = -4; k <= 4; ++k) {
                const Vec2 q = sm.p + sm.t * (k * 26.0f) + n * (sm.halfWidth + edge + 18.0f);
                if (!clearOfTrack(q, 6)) continue;
                add(P_PYRAMID, W(q, 17.0f), {13, 5.5f, 9}, yawOf(sm.t) + 0.3f * k, {236, 236, 240, 255});
            }
        }
        const Vec2 city = ctr + Vec2{half + 900, half * 0.4f};
        for (int k = 0; k < 2; ++k) {  // the twin towers
            const Vec2 q = city + Vec2{0, k * 60.0f};
            add(P_CYL, W(q), {16, 340, 16}, 0, {196, 200, 210, 255});
            add(P_CONE, W(q, 340), {10, 60, 10}, 0, {210, 214, 222, 255});
        }
        for (int k = 0; k < 16; ++k) {
            const Vec2 q = city + Vec2{R(-300, 300), R(-400, 400)};
            const float h = R(60, 200);
            add(P_BOX, W(q, h * 0.5f), {R(25, 45), h, R(25, 45)}, R(0, PI), mix({170, 180, 196, 255}, {120, 130, 150, 255}, R(0, 1)));
        }
        woods(10, 0.55f, 240, 8, 350, [&](Vec2 p, float r) {
            if (r < 0.5f) tree(PALM, p, R(0.9f, 1.4f), mix({40, 120, 40, 255}, {70, 140, 50, 255}, R(0, 1)));
            else tree(BROADLEAF, p, R(1.2f, 1.9f), mix({30, 96, 36, 255}, {50, 120, 44, 255}, R(0, 1)));
        });
    } else if (th == "woodland") {
        // Brands Hatch: the Kent countryside. Woods, hills, oast houses and farms.
        for (int k = 0; k < 10; ++k) {
            const Vec2 p = randomPoint(400);
            if (!free(p, 110, 90)) continue;
            mound(p, R(165, 300), R(25, 45), R(165, 300), R(0, PI), {70, 112, 52, 255});
        }
        for (int k = 0; k < 5; ++k) {
            const Vec2 p = randomPoint(160);
            if (!free(p, 40, 20)) continue;
            const float yaw = R(0, PI);
            building(p, yaw, 22, 9, 6, {150, 72, 52, 255}, 1, {90, 60, 50, 255});
            const Vec2 d = {std::cos(yaw), std::sin(yaw)};
            for (int j = 0; j < 2; ++j) {  // oast kilns: round brick towers with white cowls
                const Vec2 q = p + d * (14.0f + 9.0f * j);
                add(P_CYL, W(q), {4.2f, 9.0f, 4.2f}, 0, {160, 76, 54, 255});
                add(P_CONE, W(q, 9.0f), {4.6f, 6.5f, 4.6f}, 0, {120, 70, 52, 255});
                add(P_BOX, W(q, 16.2f), {1.6f, 2.0f, 1.6f}, yaw, {240, 240, 236, 255});
            }
        }
        woods(10, 0.55f, 240, 8, 350, [&](Vec2 p, float r) {
            tree(r < 0.8f ? BROADLEAF : CONIFER, p, R(1.0f, 1.6f), mix(leafGreen, leafDark, R(0, 1)));
        });
    } else {
        // Circuit Raylib and anything without a theme: a modern circuit in the countryside.
        for (int k = 0; k < 6; ++k) {
            const Vec2 p = randomPoint(60);
            if (!free(p, 35, 15)) continue;
            building(p, R(0, PI), R(20, 34), R(14, 20), R(6, 10), {222, 224, 228, 255}, 0, {50, 90, 170, 255});
        }
        Vec2 spot;
        if (findSpot(0, (float)pitSide, 70, 8, spot)) {  // race control tower
            add(P_CYL, W(spot), {4, 22, 4}, 0, {200, 202, 208, 255});
            add(P_BOX, W(spot, 24), {14, 5, 14}, R(0, PI), {60, 120, 190, 255});
            feet.push_back({spot, 12});
        }
        woods(12, 0.45f, 260, 9, 350, [&](Vec2 p, float r) {
            tree(r < 0.6f ? CONIFER : BROADLEAF, p, R(0.8f, 1.5f), r < 0.6f ? mix(pine, leafGreen, R(0, 1)) : leafGreen);
        });
    }

    // tile bounds, for culling
    for (TreeTile& t : treeTiles_) {
        Vector3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
        for (const auto& v : t.parts)
            for (const Matrix& m : v) {
                lo = Vector3Min(lo, {m.m12, m.m13, m.m14});
                hi = Vector3Max(hi, {m.m12, m.m13, m.m14});
            }
        t.centre = Vector3Scale(Vector3Add(lo, hi), 0.5f);
        t.radius = Vector3Distance(lo, hi) * 0.5f + 25.0f;
    }
}

void Renderer::drawProps(bool shadowPass) {
    const Vector3 cam = camera.position;
    const Vector3 fwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    for (const Prop& p : props_) {
        if (shadowPass) {
            const float dx = p.pos.x - shadowCentre_.x, dz = p.pos.z - shadowCentre_.z;
            if (dx * dx + dz * dz > (shadowRadius_ + p.radius) * (shadowRadius_ + p.radius)) continue;
        } else {
            const Vector3 v = Vector3Subtract(p.pos, cam);
            const float d = Vector3Length(v);
            if (d > 3000 + p.radius) continue;
            if (d > p.radius && Vector3DotProduct(v, fwd) < -p.radius) continue;  // behind the camera
        }
        const Matrix T = MatrixTranslate(p.pos.x, p.pos.y, p.pos.z);
        const Matrix Y = MatrixRotateY(p.yaw);
        switch (p.kind) {
            case P_BOX:
            case P_MOUND:
            case P_SPHERE: {
                const Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(p.size.x, p.size.y, p.size.z), MatrixRotateX(p.tilt)),
                                                MatrixMultiply(Y, T));
                drawModel(p.kind == P_BOX ? mdlCube_ : mdlSphere_, M, p.color);
                break;
            }
            case P_CYL:
            case P_CONE:
                drawModel(p.kind == P_CYL ? mdlTrunk_ : mdlCone_,
                          MatrixMultiply(MatrixScale(p.size.x, p.size.y, p.size.z), MatrixMultiply(Y, T)), p.color);
                break;
            case P_PYRAMID: {
                const Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateY(PI / 4), MatrixScale(p.size.x * 1.4142f, p.size.y,
                                                                                                    p.size.z * 1.4142f)),
                                                MatrixMultiply(Y, T));
                drawModel(mdlPyramid_, M, p.color);
                break;
            }
            case P_ROOF: {  // a half cylinder lying along the building
                Matrix M = MatrixMultiply(MatrixScale(p.size.z, p.size.y, p.size.x), MatrixRotateZ(-PI / 2));
                M = MatrixMultiply(M, MatrixTranslate(-p.size.y * 0.5f, 0, 0));
                drawModel(mdlTrunk_, MatrixMultiply(M, MatrixMultiply(Y, T)), p.color);
                break;
            }
        }
    }
}

void Renderer::drawTrees(bool shadowPass) {
    const Vector3 cam = camera.position;
    const Vector3 fwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    treeBatch_.resize(treeSlots());
    for (auto& b : treeBatch_) b.clear();
    for (const TreeTile& t : treeTiles_) {
        if (shadowPass) {
            const float dx = t.centre.x - shadowCentre_.x, dz = t.centre.z - shadowCentre_.z;
            if (dx * dx + dz * dz > (shadowRadius_ + t.radius) * (shadowRadius_ + t.radius)) continue;
        } else {
            const Vector3 v = Vector3Subtract(t.centre, cam);
            const float d = Vector3Length(v);
            if (d > 3000 + t.radius) continue;
            if (d > t.radius && Vector3DotProduct(v, fwd) < -t.radius) continue;
        }
        // far away, the trunks and the smaller clumps of leaves are left out
        const bool far = !shadowPass && Vector3Distance(t.centre, cam) > 900 + t.radius;
        for (int k = 0; k < (int)t.parts.size(); ++k) {
            if (far && (k == TP_TRUNK || k == TP_BLOB2)) continue;
            treeBatch_[k].insert(treeBatch_[k].end(), t.parts[k].begin(), t.parts[k].end());
        }
    }
    treeMat_.shader = shadowPass ? depthInst_ : litInst_;
    const unsigned whiteTex = rlGetTextureIdDefault();
    rlDisableBackfaceCulling();  // leaf cards are seen from both sides
    for (int k = 0; k < treeSlots(); ++k) {
        if (treeBatch_[k].empty()) continue;
        const bool model = k >= TP_COUNT;
        treeMat_.maps[MATERIAL_MAP_DIFFUSE].texture =
            model ? slotTex_[k - TP_COUNT] : Texture2D{whiteTex, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        const Mesh& mesh = model ? slotMesh_[k - TP_COUNT] : treeMesh_[k == TP_BLOB2 ? TP_BLOB : k];
        DrawMeshInstanced(mesh, treeMat_, treeBatch_[k].data(), (int)treeBatch_[k].size());
    }
    rlEnableBackfaceCulling();
    treeMat_.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{whiteTex, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

// The tree models in assetsDir/scenery/trees (tools/import_trees.py). Without them the
// scenery falls back to trees built from simple shapes.
void Renderer::loadTrees(const std::string& assetsDir) {
    const std::string dir = assetsDir + "/scenery/trees/";
    char* text = assetsDir.empty() ? nullptr : LoadFileText((dir + "trees.json").c_str());
    if (!text) return;
    const mjson::Value root = mjson::parse(text);
    UnloadFileText(text);
    std::vector<std::string> texNames;
    const mjson::Value& vs = root["variants"];
    for (size_t i = 0; i < vs.size(); ++i) {
        TreeModel tm;
        tm.kind = vs[i]["kind"].str();
        const mjson::Value& parts = vs[i]["parts"];
        for (size_t j = 0; j < parts.size(); ++j) {
            Model m = LoadModel((dir + parts[j]["mesh"].str()).c_str());
            if (m.meshCount < 1) continue;
            // keep the mesh, drop the model's own material
            slotMesh_.push_back(m.meshes[0]);
            m.meshCount = 0;
            MemFree(m.meshes);
            m.meshes = nullptr;
            UnloadModel(m);
            const std::string tn = parts[j]["texture"].str();
            size_t t = std::find(texNames.begin(), texNames.end(), tn) - texNames.begin();
            if (t == texNames.size()) {
                Texture2D tex = LoadTexture((dir + tn).c_str());
                GenTextureMipmaps(&tex);
                SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
                texNames.push_back(tn);
                treeTextures_.push_back(tex);
            }
            slotTex_.push_back(treeTextures_[t]);
            tm.slots.push_back(TP_COUNT + (int)slotMesh_.size() - 1);
        }
        if (!tm.slots.empty()) treeModels_.push_back(tm);
    }
}
