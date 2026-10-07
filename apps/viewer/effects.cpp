#include "effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdio>

#include "raymath.h"
#include "rlgl.h"

namespace {
const size_t kSkidCap = 6000, kPartCap = 3000;
const float kWheelX = 1.65f, kWheelY = 0.78f;  // half wheelbase, half track (m)
Vector3 W(rr::Vec2 p, float h = 0) { return {p.x, h, -p.y}; }

void wheelPos(const rr::Car& c, Vector3 out[4], float h) {
    const float cy = std::cos(c.state.yaw), sy = std::sin(c.state.yaw);
    const float lx[4] = {kWheelX, kWheelX, -kWheelX, -kWheelX}, ly[4] = {kWheelY, -kWheelY, kWheelY, -kWheelY};
    for (int w = 0; w < 4; ++w)
        out[w] = W(c.state.pos + rr::Vec2{lx[w] * cy - ly[w] * sy, lx[w] * sy + ly[w] * cy}, h);
}
}  // namespace

void Effects::reset(const rr::Race& race) {
    race_ = &race;
    rubber_.assign((size_t)(race.track().length() / kCellS + 1) * kLanes, 0.0f);
    rubberCells_.clear();
    skids_.clear(); parts_.clear();
    skidHead_ = 0;
    last_.assign(race.cars().size(), Wheels{});
    for (size_t i = 0; i < race.cars().size(); ++i) last_[i].collisions = race.cars()[i].collisions;
}

void Effects::addMark(std::vector<Mark>& ring, size_t& head, size_t cap, Vector3 a, Vector3 b, float w, unsigned char alpha) {
    Mark m{a, b, w, alpha};
    if (ring.size() < cap) ring.push_back(m);
    else ring[head] = m;
    head = (head + 1) % cap;
}

void Effects::emit(int kind, Vector3 p, Vector3 v, int count) {
    auto rnd = [&]() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.0f; };
    for (int k = 0; k < count && parts_.size() < kPartCap; ++k) {
        Particle q{};
        q.p = p;
        q.kind = kind;
        if (kind == 0) {  // tyre smoke: drifts up, grows, fades
            q.v = Vector3Add(Vector3Scale(v, 0.25f), {rnd() - 0.5f, 0.6f + rnd() * 0.6f, rnd() - 0.5f});
            q.life = 1.4f + rnd(); q.size = 0.4f; q.col = {230, 230, 232, 55};
        } else if (kind == 1) {  // dust and grass thrown up
            q.v = Vector3Add(Vector3Scale(v, 0.35f), {2 * rnd() - 1, 1.0f + rnd() * 1.5f, 2 * rnd() - 1});
            q.life = 1.2f + rnd(); q.size = 0.6f; q.col = {150, 128, 92, 120};
        } else {  // sparks: fast, short, fall
            q.v = Vector3Add(Vector3Scale(v, 0.6f), {6 * rnd() - 3, 1.5f + rnd() * 3, 6 * rnd() - 3});
            q.life = 0.35f + 0.3f * rnd(); q.size = 0.06f; q.col = {255, 190, 70, 255};
        }
        parts_.push_back(q);
    }
}

void Effects::update(const rr::Race& race, float dt) {
    if (race_ != &race || race.time() < lastTime_ || last_.size() != race.cars().size()) reset(race);
    const bool moved = race.time() > lastTime_;
    lastTime_ = race.time();
    // particles live in real time, so they still drift while paused
    for (auto& q : parts_) {
        q.age += dt;
        if (q.kind == 2) q.v.y -= 9.8f * dt;
        else q.v = Vector3Scale(q.v, 1.0f - std::min(1.0f, 1.2f * dt));
        q.p = Vector3Add(q.p, Vector3Scale(q.v, dt));
        if (q.kind == 0 || q.kind == 1) q.size += dt * (q.kind == 0 ? 1.6f : 1.0f);
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& q) { return q.age >= q.life; }), parts_.end());
    if (level_ <= 0 || !moved) return;

    for (size_t i = 0; i < race.cars().size(); ++i) {
        const rr::Car& c = race.cars()[i];
        Wheels& L = last_[i];
        Vector3 now[4];
        wheelPos(c, now, 0.0f);
        const float speed = std::hypot(c.state.vx, c.state.vy);
        if (c.dnf || c.pitState != RR_PIT_NONE) { L.valid = false; continue; }
        const float jump = L.valid ? Vector3Distance(now[2], L.p[2]) : 0.0f;
        const bool join = L.valid && jump < 25.0f;  // a fast-forward or a reset leaves a gap
        const bool sliding = c.state.slipAngle[0] > 1.15f || c.state.slipAngle[1] > 1.15f;
        const bool locking = c.control.brake > 0.85f && speed > 15 && c.state.ax < -30;
        const bool spinning = std::fabs(c.state.wheelSpin) > 0.25f;
        if (join && c.onTrack) {
            // rubber builds up on the line the cars drive
            const int cols = (int)(rubber_.size() / kLanes);
            for (float side : {-kWheelY, kWheelY}) {
                const int is = ((int)std::floor(c.trackS / kCellS) % cols + cols) % cols;
                const int il = (int)std::floor((c.lateral + side) / kCellL) + kLanes / 2;
                if (il < 0 || il >= kLanes) continue;
                float& r = rubber_[(size_t)is * kLanes + il];
                if (r == 0.0f) rubberCells_.push_back(is * kLanes + il);
                r = std::min(1.0f, r + 0.008f * std::min(jump, 4.0f));
            }
            if (sliding || locking || spinning) {
                const int from = locking ? 0 : (spinning ? 2 : 0);
                for (int w = from; w < 4; ++w) {
                    Vector3 a = L.p[w], b = now[w];
                    a.y = b.y = 0.009f;
                    addMark(skids_, skidHead_, kSkidCap, a, b, 0.30f, 150);
                }
            }
        }
        if (level_ >= 2 && !c.onTrack && speed > 6) {
            Vector3 v = W(c.state.velWorld());
            emit(1, Vector3Lerp(now[2], now[3], 0.5f), v, 2);
        }
        if (level_ >= 2 && c.collisions > L.collisions) {
            Vector3 v = W(c.state.velWorld());
            Vector3 p = W(c.state.pos, 0.3f);
            emit(2, p, v, 40);
        }
        L.collisions = c.collisions;
        for (int w = 0; w < 4; ++w) L.p[w] = now[w];
        L.valid = true;
    }
}

void Effects::draw(Vector3 camPos) const {
    if (level_ <= 0) return;
    auto quad = [](const std::vector<Mark>& ring, Color base) {
        size_t n = 0;
        rlBegin(RL_TRIANGLES);
        for (const Mark& m : ring) {
            if (++n % 1000 == 0) { rlEnd(); rlDrawRenderBatchActive(); rlBegin(RL_TRIANGLES); }
            Vector3 d = Vector3Subtract(m.b, m.a);
            float len = std::sqrt(d.x * d.x + d.z * d.z);
            if (len < 1e-3f) continue;
            Vector3 n = {-d.z / len * m.width * 0.5f, 0, d.x / len * m.width * 0.5f};
            Vector3 a0 = Vector3Add(m.a, n), a1 = Vector3Subtract(m.a, n);
            Vector3 b0 = Vector3Add(m.b, n), b1 = Vector3Subtract(m.b, n);
            rlColor4ub(base.r, base.g, base.b, m.alpha);
            rlVertex3f(a0.x, a0.y, a0.z); rlVertex3f(b1.x, b1.y, b1.z); rlVertex3f(b0.x, b0.y, b0.z);
            rlVertex3f(a0.x, a0.y, a0.z); rlVertex3f(a1.x, a1.y, a1.z); rlVertex3f(b1.x, b1.y, b1.z);
        }
        rlEnd();
    };
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    if (race_) {
        const rr::Track& tr = race_->track();
        size_t n = 0;
        rlBegin(RL_TRIANGLES);
        for (int cell : rubberCells_) {
            if (++n % 1000 == 0) { rlEnd(); rlDrawRenderBatchActive(); rlBegin(RL_TRIANGLES); }
            const float s0 = (cell / kLanes) * kCellS, l0 = (cell % kLanes - kLanes / 2) * kCellL;
            Vector3 p00 = W(tr.pointAt(s0, l0), 0.006f), p10 = W(tr.pointAt(s0 + kCellS, l0), 0.006f);
            Vector3 p01 = W(tr.pointAt(s0, l0 + kCellL), 0.006f), p11 = W(tr.pointAt(s0 + kCellS, l0 + kCellL), 0.006f);
            rlColor4ub(18, 18, 20, (unsigned char)(rubber_[cell] * 150));
            rlVertex3f(p00.x, p00.y, p00.z); rlVertex3f(p11.x, p11.y, p11.z); rlVertex3f(p10.x, p10.y, p10.z);
            rlVertex3f(p00.x, p00.y, p00.z); rlVertex3f(p01.x, p01.y, p01.z); rlVertex3f(p11.x, p11.y, p11.z);
        }
        rlEnd();
    }
    quad(skids_, {18, 18, 18, 255});
    if (level_ >= 2) {
        for (const Particle& q : parts_) {
            float t = q.age / q.life;
            Color c = q.col;
            c.a = (unsigned char)(c.a * (1.0f - t));
            if (q.kind == 2) DrawCubeV(q.p, {q.size, q.size, q.size}, c);
            else if (Vector3Distance(q.p, camPos) < 250) DrawSphereEx(q.p, q.size, 4, 6, c);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
