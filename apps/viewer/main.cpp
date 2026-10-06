// rr_viewer: runs a race and shows it in 3D. Same command line as rr_race.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "hud.hpp"
#include "race.hpp"
#include "raylib.h"
#include "renderer.hpp"
#include "rlgl.h"

#ifndef RR_SOURCE_DIR
#define RR_SOURCE_DIR "."
#endif

namespace {

struct Paths {
    std::vector<std::string> bots, tracks;
    std::string assets;
};

std::unique_ptr<rr::Race> makeRace(const rr::RaceConfig& cfg, const Paths& paths) {
    auto race = std::make_unique<rr::Race>();
    std::string err;
    if (!race->setup(cfg, paths.bots, paths.tracks, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return nullptr;
    }
    return race;
}

}  // namespace

int main(int argc, char** argv) {
    rr::RaceConfig cfg;
    bool help = false;
    std::string err;
    if (!rr::parseArgs(argc, argv, cfg, true, help, &err)) {
        std::fprintf(stderr, "error: %s\n\n%s", err.c_str(), rr::usage(argv[0], true).c_str());
        return 2;
    }
    if (help) {
        std::printf("%s", rr::usage(argv[0], true).c_str());
        return 0;
    }
    if (cfg.entries.empty())
        cfg.entries = {{"racingline", "", ""}, {"gapfollow", "", ""}, {"simple", "", ""},
                       {"racingline", "grip=0.75", "racingline (safe)"}, {"gapfollow", "speed=0.85", "gapfollow (safe)"}};

    const std::string dir = rr::exeDir(argv[0]);
    Paths paths;
    paths.bots = {dir + "/bots", dir, "bots", "."};
    paths.tracks = {dir + "/tracks", RR_SOURCE_DIR "/tracks", "tracks"};
    for (const std::string& a : {dir + "/assets", std::string(RR_SOURCE_DIR "/assets"), std::string("assets")})
        if (std::filesystem::exists(a + "/fonts")) { paths.assets = a; break; }

    auto race = makeRace(cfg, paths);
    if (!race) return 1;
    for (const auto& w : race->track().warnings()) std::fprintf(stderr, "track warning: %s\n", w.c_str());

    const bool shotMode = !cfg.screenshot.empty();
    SetTraceLogLevel(LOG_WARNING);
    unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!shotMode) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    InitWindow(cfg.width, cfg.height, "Raylib Racers");
    if (cfg.fullscreen) ToggleFullscreen();
    SetTargetFPS(shotMode ? 0 : 120);

    Renderer renderer;
    if (!renderer.init(race->track(), (unsigned)cfg.seed, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        CloseWindow();
        return 1;
    }
    Hud hud;
    hud.init(race->track(), paths.assets);

    HudState st;
    st.timeScale = cfg.timeScale;
    st.camera = (CamMode)(std::max(0, cfg.camera) % CAM_COUNT);
    double simDebt = 0;
    int shotFrames = 0;

    if (shotMode) {
        while (!race->isOver() && race->time() < cfg.screenshotAt) race->step();
        st.focus = race->order()[0];
    }
    if (cfg.focus >= 0 && cfg.focus < (int)race->cars().size()) st.focus = cfg.focus;

    while (!WindowShouldClose()) {
        const float frameDt = std::min(GetFrameTime(), 0.1f);
        const int n = (int)race->cars().size();

        // ---- input
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_RIGHT)) st.focus = (st.focus + 1) % n;
        if (IsKeyPressed(KEY_LEFT)) st.focus = (st.focus + n - 1) % n;
        for (int k = 0; k < 9 && k < n; ++k)
            if (IsKeyPressed(KEY_ONE + k)) st.focus = race->order()[k];
        if (IsKeyPressed(KEY_C)) st.camera = (CamMode)((st.camera + 1) % CAM_COUNT);
        if (IsKeyPressed(KEY_SPACE)) st.paused = !st.paused;
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) st.timeScale = std::min(64.0f, st.timeScale * 2);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) st.timeScale = std::max(0.125f, st.timeScale / 2);
        if (IsKeyPressed(KEY_P)) st.view.showPaths = !st.view.showPaths;
        if (IsKeyPressed(KEY_S)) st.view.showSensors = !st.view.showSensors;
        if (IsKeyPressed(KEY_H)) st.showHud = !st.showHud;
        if (IsKeyPressed(KEY_F1)) st.showHelp = !st.showHelp;
        if (IsKeyPressed(KEY_R)) {
            auto fresh = makeRace(cfg, paths);
            if (fresh) {
                race = std::move(fresh);
                simDebt = 0;
                st.focus = std::min(st.focus, (int)race->cars().size() - 1);
            }
        }

        // ---- simulation: fixed steps, as many as real time x speed asks for
        if (!shotMode) {
            if (!st.paused) {
                simDebt += frameDt * st.timeScale;
                long long steps = (long long)(simDebt / race->dt());
                simDebt -= steps * race->dt();
                for (long long i = 0; i < steps && !race->isOver(); ++i) race->step();
            } else if (IsKeyPressed(KEY_N)) {
                race->advance(1.0 / race->config().robotHz);
            }
        }

        renderer.updateCamera(*race, st.focus, st.camera, shotMode ? 1.0f / 60 : frameDt);

        BeginDrawing();
        ClearBackground(BLACK);
        renderer.draw(*race, st.focus, st.view);
        hud.draw(*race, st);
        if (shotMode && ++shotFrames == 3) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            bool ok = ExportImage(img, cfg.screenshot.c_str());
            UnloadImage(img);
            EndDrawing();
            std::printf("%s %s at t=%.1fs\n", ok ? "saved" : "FAILED to save", cfg.screenshot.c_str(), race->time());
            break;
        }
        EndDrawing();
    }

    if (!shotMode && race->isOver() && !cfg.quiet) race->printResults(stdout);
    hud.shutdown();
    renderer.shutdown();
    CloseWindow();
    return 0;
}
