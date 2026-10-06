#pragma once
#include <string>
#include <vector>

#include "raylib.h"
#include "menu.hpp"
#include "race.hpp"
#include "renderer.hpp"

struct HudState {
    bool paused = false;
    float timeScale = 1.0f;
    int focus = 0;
    bool followLeader = true;  // keep the camera on whoever is P1
    CamMode camera = CAM_CHASE;
    bool showHud = true;
    bool showHelp = false;
    bool muted = false;
    ViewOptions view;
};

// 2D overlay: timing tower, minimap, focused-car telemetry, help and results.
class Hud {
public:
    void init(const rr::Track& track, const std::string& assetsDir);
    void shutdown();
    void draw(const rr::Race& race, const HudState& st);
    // The race setup screen; fills `hits` with its clickable areas.
    void drawMenu(const MenuState& m, std::vector<MenuHit>& hits);

private:
    void text(const char* s, float x, float y, float size, Color c, bool bold = false, bool mono = false);
    float width(const char* s, float size, bool bold = false, bool mono = false);
    void textRight(const char* s, float right, float y, float size, Color c, bool bold = false, bool mono = false);
    void panel(Rectangle r, float alpha = 0.62f);

    void drawTower(const rr::Race& race, const HudState& st);
    void drawMinimap(const rr::Race& race, const HudState& st);
    void drawCarPanel(const rr::Race& race, const HudState& st);
    void drawHelp();
    void drawResults(const rr::Race& race);

    Font regular_{}, bold_{}, mono_{};
    bool ownFonts_ = false;
    std::vector<rr::Vec2> outline_;  // centreline samples for the minimap
    float minX_ = 0, minY_ = 0, maxX_ = 1, maxY_ = 1, trackWidth_ = 14;
};
