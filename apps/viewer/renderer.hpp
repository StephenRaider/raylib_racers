#pragma once
#include <string>
#include <vector>

#include "car_model.hpp"
#include "raylib.h"
#include "race.hpp"

// Follow, cinematic, trackside TV, helicopter, top-down, free orbit, whole-track overview.
enum CamMode { CAM_CHASE = 0, CAM_CINEMATIC, CAM_TV, CAM_HELI, CAM_TOP, CAM_ORBIT, CAM_OVERVIEW, CAM_COUNT };
const char* camName(CamMode mode);

struct ViewOptions {
    bool showPaths = true;    // robots' debug_path polylines
    bool showSensors = false; // focused car's range finders
};

Color teamColor(int carIndex);
Color teamAccent(int carIndex);

// 3D scene: track, scenery and cars, lit by a sun with shadow mapping and fog.
class Renderer {
public:
    // The F1 car comes from assetsDir/cars/f1_gearari; without it, cars are drawn from boxes.
    bool init(const rr::Track& track, unsigned seed, const std::string& assetsDir, std::string* err);
    void shutdown();

    void updateCamera(const rr::Race& race, int focus, CamMode mode, float dt);
    void draw(const rr::Race& race, int focus, const ViewOptions& opt);

    Camera3D camera{};

private:
    void buildTrack(const rr::Track& track);
    void buildScenery(const rr::Track& track, unsigned seed);
    void drawScene(const rr::Race& race, bool shadowPass);
    void drawCar(const rr::Car& car, int index);
    void drawBoxCar(const rr::Car& car, int index);
    void resetCinematic(float clock);
    void drawModel(Model& m, Matrix transform, Color tint);
    void drawBox(Vector3 center, Vector3 size, Color color, Matrix parent);

    Shader lit_{};
    Shader depth_{};
    Shader* current_ = nullptr;
    int locLightVP_ = -1, locShadowMap_ = -1, locViewPos_ = -1, locSpec_ = -1, locFog_ = -1;
    RenderTexture2D shadowMap_{};
    const int shadowRes_ = 2048;
    Matrix lightVP_{};

    Texture2D texAsphalt_{}, texGrass_{}, texChecker_{}, texWhite_{};
    Model mdlAsphalt_{}, mdlMarkings_{}, mdlWalls_{}, mdlGround_{}, mdlStart_{};
    Model mdlCube_{}, mdlWheel_{}, mdlSphere_{}, mdlCone_{}, mdlTrunk_{};
    CarModel carModel_;

    struct Tree { Vector3 pos; float scale; float tint; };
    std::vector<Tree> trees_;
    struct Box { Vector3 center, size; Color color; };
    std::vector<Box> boxes_;  // static scenery; size is (along track, up, across)
    int numGantryBoxes_ = 0;  // the first boxes use the start-line heading
    float gantryYaw_ = 0, standYaw_ = 0;
    std::vector<Vector3> tvSpots_;

    // camera state
    Vector3 chasePos_{}, chaseTarget_{};
    bool chaseInit_ = false;
    int lastFocus_ = -1;
    CamMode lastMode_ = CAM_COUNT;
    Vector3 smoothDir_{1, 0, 0};
    // cinematic: the camera eases between keyframed spots around the car, each held for a few seconds
    struct CineKey { float azimuth, height, dist, fov, hold; };
    CineKey cineFrom_{}, cineTo_{};
    float cineClock_ = 0, cineSegStart_ = 0;
    unsigned cineSeed_ = 0;
    float heliYaw_ = 0, heliDist_ = 60.0f, topHeight_ = 105.0f;
    float orbitYaw_ = 0.6f, orbitPitch_ = 0.35f, orbitDist_ = 14.0f;
    Vector3 trackCenter_{};
    float trackExtent_ = 500;
};
