#pragma once
#include <map>
#include <string>

#include "raylib.h"

// The low-poly F1 car from assets/cars/f1_gearari: a body, separate front and rear wheels, and one
// livery texture per car slot (see liveries.hpp). See assets/cars/f1_gearari/README.md for the conventions.
class CarModel {
public:
    bool load(const std::string& assetsDir, std::string* err);
    void unload();
    bool loaded() const { return loaded_; }
    float wheelRadius() const { return wheelRadius_; }

    // Pose of one car. `world` places the sim's local frame (x forward, y up, z right, origin at the CG);
    // `centreOffset` is how far ahead of the CG the wheelbase centre sits.
    struct Pose {
        Matrix world;
        float centreOffset;
        float steer;      // road-wheel angle, radians, positive = left
        float wheelRot;   // rolled angle, radians, positive = forward
        int livery;       // slot in liveryTable()
    };
    // Draws with `shader` on every material (the lit shader, or the depth-only one for shadows).
    void draw(const Pose& pose, Shader shader);

private:
    void drawPart(Model& m, Matrix transform, Shader shader);
    Texture2D livery(int slot);

    Model body_{}, wheelFront_{}, wheelRear_{};
    int liveryMaterial_ = -1;
    Texture2D defaultLivery_{};
    std::map<int, Texture2D> liveries_;  // by slot, loaded when first drawn
    struct Hub { float x, y, z; bool front; };
    Hub hubs_[4]{};
    float wheelRadius_ = 0.36f;
    bool loaded_ = false;
};
