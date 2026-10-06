#pragma once
#include <mutex>
#include <vector>

// Synthesised engine sound: a mid-2000s F1 V10 for each audible car, built from the
// engine orders of its rpm (firing pulses, crank and cam irregularity), intake and
// exhaust noise, overrun crackle, a rev-limiter stutter and some wind and tyre noise.
// It has no raylib dependency, so it can also render offline to a WAV file.
class EngineSynth {
public:
    static constexpr int kMaxCars = 32;
    static constexpr int kRate = 44100;

    // What one car sounds like right now, as heard from the listener.
    struct Voice {
        int car = -1;          // car index, keeps each car's oscillator state
        float rpm = 0;
        float throttle = 0;    // 0..1
        float speed = 0;       // m/s, for wind and tyre noise
        float maxRpm = 19000;
        float gain = 0;        // distance attenuation, 0..1
        float pan = 0;         // -1 left .. 1 right
        float pitch = 1;       // Doppler factor
    };

    // Main thread: replace the audible voices (at most kMaxVoices are used).
    void setVoices(const std::vector<Voice>& voices, float masterGain);
    // Audio thread (or offline): render interleaved stereo float samples.
    void render(float* out, int frames);

    static constexpr int kMaxVoices = 6;

private:
    struct CarState {
        double phase = 0;      // engine cycle (two crank revolutions), 0..1
        float rpm = 0, throttle = 0, gain = 0, pan = 0, pitch = 1, speed = 0, maxRpm = 19000;
        float lp1 = 0, lp2 = 0, noiseLp = 0, noiseHp = 0, windLp = 0;
        float pop = 0, limiter = 1, limiterClock = 0;
        bool live = false;
    };

    std::mutex mutex_;
    Voice pending_[kMaxVoices];
    int pendingCount_ = 0;
    float pendingMaster_ = 0;
    bool dirty_ = false;

    // owned by the rendering thread
    Voice target_[kMaxVoices];
    int targetCount_ = 0;
    float master_ = 0, masterTarget_ = 0;
    CarState cars_[kMaxCars];
    unsigned rng_ = 0x1234567u;

    float noise();
};

// Writes interleaved stereo float samples as a 16-bit WAV file.
bool writeWav(const char* path, const std::vector<float>& stereo, int rate);
