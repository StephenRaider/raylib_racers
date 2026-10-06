#include "engine_sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr float kTwoPi = 6.28318530718f;

// Engine orders, in multiples of the cycle frequency (rpm / 120). A V10 fires ten
// times per cycle, so orders 10, 20, 30... carry most of the energy; the others come
// from cylinder-to-cylinder differences and give the sound its rasp.
constexpr int kOrders = 64;

struct OrderTable {
    float base[kOrders + 1];    // amplitude off throttle
    float bright[kOrders + 1];  // extra amplitude on throttle
    OrderTable() {
        unsigned h = 0x9e3779b9u;
        for (int j = 1; j <= kOrders; ++j) {
            h = h * 1664525u + 1013904223u;
            const float r = (h >> 8) / 16777216.0f;
            float a;
            if (j % 10 == 0) a = 1.0f / (1.0f + 0.12f * (j / 10 - 1));   // firing harmonics
            else if (j % 5 == 0) a = 0.32f;                               // bank-to-bank
            else if (j % 2 == 0) a = 0.05f + 0.07f * r;                   // crank orders
            else a = 0.02f + 0.05f * r;                                   // cam orders
            base[j] = a * (j < 10 ? 0.55f : 1.0f);
            bright[j] = a * (j >= 20 ? 0.9f : 0.35f);
        }
        base[0] = bright[0] = 0;
    }
};
const OrderTable kTable;

float softClip(float x) { return std::tanh(x); }

}  // namespace

float EngineSynth::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ >> 8) / 8388608.0f - 1.0f;
}

void EngineSynth::setVoices(const std::vector<Voice>& voices, float masterGain) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingCount_ = std::min((int)voices.size(), kMaxVoices);
    for (int i = 0; i < pendingCount_; ++i) pending_[i] = voices[i];
    pendingMaster_ = masterGain;
    dirty_ = true;
}

void EngineSynth::render(float* out, int frames) {
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (lock.owns_lock() && dirty_) {
            targetCount_ = pendingCount_;
            for (int i = 0; i < targetCount_; ++i) target_[i] = pending_[i];
            masterTarget_ = pendingMaster_;
            dirty_ = false;
        }
    }
    // Which cars are audible; the others fade out.
    bool wanted[kMaxCars] = {};
    for (int i = 0; i < targetCount_; ++i) {
        const Voice& v = target_[i];
        if (v.car < 0 || v.car >= kMaxCars) continue;
        CarState& c = cars_[v.car];
        wanted[v.car] = true;
        if (!c.live) {  // start a new voice at its current state, silent
            c = CarState{};
            c.rpm = v.rpm;
            c.throttle = v.throttle;
            c.pan = v.pan;
            c.pitch = v.pitch;
            c.speed = v.speed;
            c.live = true;
        }
    }

    const float dt = 1.0f / kRate;
    // per-sample smoothing: rpm ~8 ms (seamless shifts stay quick), throttle ~25 ms, gain ~40 ms
    const float kRpm = 1 - std::exp(-dt / 0.008f), kThr = 1 - std::exp(-dt / 0.025f), kGain = 1 - std::exp(-dt / 0.04f);
    const float kMaster = 1 - std::exp(-dt / 0.15f);

    for (int f = 0; f < frames; ++f) {
        master_ += (masterTarget_ - master_) * kMaster;
        float left = 0, right = 0;
        for (int ci = 0; ci < kMaxCars; ++ci) {
            CarState& c = cars_[ci];
            if (!c.live) continue;
            const Voice* v = nullptr;
            if (wanted[ci])
                for (int i = 0; i < targetCount_; ++i)
                    if (target_[i].car == ci) { v = &target_[i]; break; }
            const float tgtGain = v ? v->gain : 0.0f;
            if (v) {
                c.rpm += (v->rpm - c.rpm) * kRpm;
                c.throttle += (v->throttle - c.throttle) * kThr;
                c.pan += (v->pan - c.pan) * kGain;
                c.pitch += (v->pitch - c.pitch) * kGain;
                c.speed += (v->speed - c.speed) * kGain;
                c.maxRpm = v->maxRpm;
            }
            c.gain += (tgtGain - c.gain) * kGain;
            if (!v && c.gain < 1e-4f) { c.live = false; continue; }

            // --- engine orders
            const float cycleHz = std::max(10.0f, c.rpm / 120.0f * c.pitch);
            c.phase += cycleHz * dt;
            c.phase -= std::floor(c.phase);
            const float th = kTwoPi * (float)c.phase;
            const float thr = c.throttle;
            // Chebyshev recurrence: sin(j th) from sin((j-1) th) and sin((j-2) th)
            const float twoCos = 2.0f * std::cos(th);
            float s2 = 0, s1 = std::sin(th);
            const int maxOrder = std::min(kOrders, (int)(16000.0f / cycleHz));
            float eng = 0;
            for (int j = 1; j <= maxOrder; ++j) {
                eng += s1 * (kTable.base[j] + thr * kTable.bright[j]);
                const float s0 = twoCos * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            eng *= 0.22f;

            // exhaust resonance: a two-pole low-pass that opens up with throttle and revs
            const float fc = 1800.0f + 9000.0f * thr * std::min(1.0f, c.rpm / c.maxRpm);
            const float a = 1 - std::exp(-kTwoPi * fc * dt);
            c.lp1 += (eng - c.lp1) * a;
            c.lp2 += (c.lp1 - c.lp2) * a;
            float sig = 0.65f * c.lp2 + 0.35f * eng;

            // intake and mechanical noise, band-passed, louder on throttle and at high revs
            const float n = noise();
            c.noiseLp += (n - c.noiseLp) * 0.35f;
            c.noiseHp += (c.noiseLp - c.noiseHp) * 0.02f;
            const float revs = std::min(1.0f, c.rpm / c.maxRpm);
            sig += (c.noiseLp - c.noiseHp) * (0.05f + 0.22f * thr) * revs;

            // overrun crackle: off throttle at high revs, random pops
            if (thr < 0.1f && c.rpm > 0.45f * c.maxRpm && noise() > 0.9993f) c.pop = 1.0f;
            sig += c.pop * noise() * 0.9f;
            c.pop *= 0.996f;

            // rev limiter: the ignition cuts in and out at ~25 Hz when pinned against it
            if (c.rpm > 0.985f * c.maxRpm && thr > 0.5f) {
                c.limiterClock += dt * 25.0f;
                c.limiter = (c.limiterClock - std::floor(c.limiterClock)) < 0.5f ? 0.35f : 1.0f;
            } else {
                c.limiter = 1.0f;
            }

            float level = (0.35f + 0.65f * thr) * (0.45f + 0.55f * revs) * c.limiter;
            float s = softClip(sig * 2.2f * level) * 0.55f;

            // wind and tyres: low-passed noise rising with speed
            c.windLp += (n - c.windLp) * 0.04f;
            s += c.windLp * std::min(1.0f, c.speed * c.speed / 9000.0f) * 0.5f;

            s *= c.gain;
            const float pl = std::sqrt(0.5f * (1 - c.pan)), pr = std::sqrt(0.5f * (1 + c.pan));
            left += s * pl;
            right += s * pr;
        }
        out[2 * f] = softClip(left * master_);
        out[2 * f + 1] = softClip(right * master_);
    }
}

bool writeWav(const char* path, const std::vector<float>& stereo, int rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t dataBytes = (uint32_t)(stereo.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (float s : stereo) {
        int16_t v = (int16_t)std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    return std::fclose(f) == 0;
}
