#pragma once
#include <vector>

#include "engine_sound.hpp"
#include "race.hpp"
#include "raylib.h"

// Engine sound for the viewer: picks the cars nearest the camera, works out how
// loud, where and how Doppler-shifted each one is, and streams EngineSynth to raylib.
class RaceAudio {
public:
    bool init();  // false when there is no audio device (the viewer runs silent)
    void shutdown();
    // Call once per frame. `active` is false in the menu, while paused or when muted.
    void update(const rr::Race& race, const Camera3D& camera, int focus, bool active, float dt);
    bool ready() const { return ready_; }

    // The voices heard from `listener` (moving at `listenerVel`, `right` = its right
    // ear direction), in world coordinates. Shared with the offline WAV renderer.
    static std::vector<EngineSynth::Voice> listen(const rr::Race& race, Vector3 listener, Vector3 listenerVel,
                                                  Vector3 right, int focus);

private:
    EngineSynth synth_;
    AudioStream stream_{};
    bool ready_ = false;
    Vector3 lastPos_{};
    bool havePos_ = false;
};
