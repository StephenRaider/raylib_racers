#pragma once
// Track and air effects, drawn in the 3D pass: rubber laid down on the racing
// line, skid marks, tyre smoke, dust off the track and sparks from contact.
// Viewer only: nothing here touches the simulation.
#include <vector>

#include "raylib.h"
#include "race.hpp"

class Effects {
public:
    void setLevel(int level) { level_ = level; }  // 0 off, 1 marks only, 2 everything
    void update(const rr::Race& race, float dt);
    void draw(Vector3 camPos) const;

private:
    struct Mark { Vector3 a, b; float width; unsigned char alpha; };
    struct Particle { Vector3 p, v; float age, life, size; Color col; int kind; };
    void reset(const rr::Race& race);
    void addMark(std::vector<Mark>& ring, size_t& head, size_t cap, Vector3 a, Vector3 b, float w, unsigned char alpha);
    void emit(int kind, Vector3 p, Vector3 v, int count);

    int level_ = 2;
    const rr::Race* race_ = nullptr;
    double lastTime_ = 0;
    // rubber: how much has been laid on each 2 m x 0.5 m cell of the track
    static constexpr float kCellS = 2.0f, kCellL = 0.5f;
    static constexpr int kLanes = 56;  // +-14 m
    std::vector<float> rubber_;
    std::vector<int> rubberCells_;  // cells with any rubber, for drawing
    std::vector<Mark> skids_;
    size_t skidHead_ = 0;
    std::vector<Particle> parts_;
    struct Wheels { Vector3 p[4]; bool valid = false; int collisions = 0; };
    std::vector<Wheels> last_;
    unsigned rng_ = 12345;
};
