// rr_viewer: runs a race and shows it in 3D. Same command line as rr_race.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>

#include "engine_sound.hpp"
#include "hud.hpp"
#include "liveries.hpp"
#include "menu.hpp"
#include "race_audio.hpp"
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

// Algorithms the grid page offers: the example robots with a few settings, then any
// other robot library found in the bot folders (your own robots show up here).
std::vector<Algorithm> listAlgorithms(const Paths& paths) {
    std::vector<Algorithm> algos = {
        // The racingline family first: the default grid uses these four.
        {"racingline", "racingline", ""},
        {"racingline aggressive", "racingline", "grip=0.85,brake=0.75,push=1.3,attack=1.4,heat=15"},
        {"racingline safe", "racingline", "grip=0.75"},
        {"racingline steady", "racingline", "grip=0.7,brake=0.6,heat=0"},
        {"gapfollow", "gapfollow", ""},
        {"gapfollow safe", "gapfollow", "speed=0.85"},
        {"gapfollow steady", "gapfollow", "speed=0.8"},
        {"simple", "simple", ""},
    };
    std::set<std::string> found;
    for (const std::string& dir : paths.bots) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const std::string ext = e.path().extension().string();
            if (ext == ".so" || ext == ".dll" || ext == ".dylib") found.insert(e.path().stem().string());
        }
    }
    for (const std::string& name : found) {
        bool known = false;
        for (const Algorithm& a : algos) known = known || a.robot == name;
        if (!known) algos.push_back({name, name, ""});
    }
    return algos;
}

// Every *.trk in the track folders, by file name.
std::vector<std::string> listTracks(const Paths& paths) {
    std::set<std::string> names;
    for (const std::string& dir : paths.tracks) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".trk") names.insert(e.path().stem().string());
    }
    return {names.begin(), names.end()};
}

// Drives three laps of `track` with the racingline robot alone (no pit stops) and
// measures a flying lap: time, fuel and tyre wear. The menu uses it to show tyre life
// and fuel range in laps of this track.
TrackStats calibrate(const std::string& track, const Paths& paths) {
    TrackStats t;
    t.file = t.title = track;
    rr::RaceConfig c;
    c.track = track;
    c.laps = 3;
    c.quiet = true;
    c.entries = {{"racingline", "pit=0", ""}};
    rr::Race r;
    std::string err;
    if (!r.setup(c, paths.bots, paths.tracks, &err)) return t;
    t.title = r.track().name();
    t.length = r.track().length();
    // fallbacks, scaled from the circuit, in case the robot does not finish
    const float k = t.length / 3176.0f;
    t.lapTime = 55 * k;
    t.fuelPerLap = 2.3f * k;
    t.wearPerLap = 0.03f * k;
    const auto wear = [](const rr::Car& car) { return std::max(car.state.tireWear[0], car.state.tireWear[1]); };
    float fuel1 = 0, wear1 = 0;
    int laps = 0;
    while (!r.isOver() && r.time() < 900) {
        r.step();
        const rr::Car& car = r.cars()[0];
        if (car.lapsDone == laps) continue;
        laps = car.lapsDone;
        if (laps == 1) {
            fuel1 = car.state.fuel;
            wear1 = wear(car);
        } else if (laps == 3) {
            t.fuelPerLap = (fuel1 - car.state.fuel) / 2;
            t.wearPerLap = (wear(car) - wear1) / 2;
            t.lapTime = car.bestLap;
            t.measured = true;
            break;
        }
    }
    return t;
}

// --sound-test: no window; races to --at, then records 25 s of the focused car's
// engine heard from just behind it, with the other cars passing by.
int runSoundTest(const rr::RaceConfig& cfg, const Paths& paths) {
    auto race = makeRace(cfg, paths);
    if (!race) return 1;
    const double t0 = std::max(0.0f, cfg.screenshotAt);
    while (!race->isOver() && race->time() < t0) race->step();
    EngineSynth synth;
    std::vector<float> pcm, buf;
    const int fps = 60, frames = EngineSynth::kRate / fps;
    buf.resize(2 * frames);
    for (int i = 0; i < 25 * fps && !race->isOver(); ++i) {
        race->advance(1.0 / fps);
        int focus = cfg.focus >= 0 && cfg.focus < (int)race->cars().size() ? cfg.focus : race->order()[0];
        const rr::Car& c = race->cars()[focus];
        rr::Vec2 h = rr::fromAngle(c.state.yaw), v = c.state.velWorld();
        Vector3 fwd = {h.x, 0, -h.y}, right = {h.y, 0, h.x};
        Vector3 pos = {c.state.pos.x - fwd.x * 7, 2.0f, -c.state.pos.y - fwd.z * 7};
        synth.setVoices(RaceAudio::listen(*race, pos, {v.x, 0, -v.y}, right, focus), 1.1f);
        synth.render(buf.data(), frames);
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }
    if (!writeWav(cfg.soundTest.c_str(), pcm, EngineSynth::kRate)) {
        std::fprintf(stderr, "error: could not write %s\n", cfg.soundTest.c_str());
        return 1;
    }
    std::printf("saved %s (%.1f s from t=%.0f s)\n", cfg.soundTest.c_str(), pcm.size() / 2.0 / EngineSynth::kRate, t0);
    return 0;
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
    const std::vector<rr::EntrySpec> cliEntries = cfg.entries;

    const std::string dir = rr::exeDir(argv[0]);
    Paths paths;
    paths.bots = {dir + "/bots", dir, "bots", "."};
    paths.tracks = {dir + "/tracks", RR_SOURCE_DIR "/tracks", "tracks"};
    for (const std::string& a : {dir + "/assets", std::string(RR_SOURCE_DIR "/assets"), std::string("assets")})
        if (std::filesystem::exists(a + "/fonts")) { paths.assets = a; break; }

    setLiveryTable(loadLiveries(paths.assets));
    const int liveryCount = std::max(1, (int)liveryTable().size());

    // The grid: one livery slot and algorithm per car. Without --car, a full field over all the teams.
    MenuState menu;
    menu.algos = listAlgorithms(paths);
    menu.liveryCount = liveryCount;
    if (cliEntries.empty()) {
        const int n = liveryTable().empty() ? 7 : liveryCount;
        for (int i = 0; i < n; ++i) menu.carAlgo.push_back(i % 4);  // the four racingline presets
    } else {
        for (const rr::EntrySpec& e : cliEntries) {
            int found = -1;
            for (int a = 0; a < (int)menu.algos.size(); ++a)
                if (menu.algos[a].robot == e.robot && menu.algos[a].params == e.params) found = a;
            if (found < 0) {
                menu.algos.push_back({e.name.empty() ? e.robot : e.name, e.robot, e.params});
                found = (int)menu.algos.size() - 1;
            }
            menu.carAlgo.push_back(found);
        }
    }
    menu.maxCars = (int)menu.carAlgo.size();
    while ((int)menu.carAlgo.size() < liveryCount) menu.carAlgo.push_back((int)menu.carAlgo.size() % 4);
    menu.maxCars = std::max(menu.maxCars, liveryCount);
    for (int i = 0; i < menu.maxCars; ++i) menu.carLivery.push_back(i % liveryCount);
    menu.cars = cliEntries.empty() ? (int)std::min<size_t>(menu.maxCars, liveryTable().empty() ? 7 : liveryCount)
                                   : (int)cliEntries.size();
    // Entries for the first `cars` cars of the grid; names carry the race number.
    auto gridEntries = [&]() {
        std::vector<rr::EntrySpec> entries;
        std::vector<int> slots;
        for (int i = 0; i < menu.cars; ++i) {
            const Algorithm& a = menu.algos[menu.carAlgo[i]];
            const int slot = menu.carLivery[i];
            std::string name = a.label;
            if (!liveryTable().empty()) name = std::to_string(liveryTable()[slot].number) + " " + name;
            entries.push_back({a.robot, a.params, name});
            slots.push_back(slot);
        }
        setCarLiveries(slots);
        return entries;
    };
    cfg.entries = gridEntries();

    if (!cfg.soundTest.empty()) return runSoundTest(cfg, paths);

    const bool shotMode = !cfg.screenshot.empty();
    // a screenshot without --at shows the menu
    bool inMenu = !cfg.noMenu && (!shotMode || cfg.screenshotAt < 0);

    // Menu: tracks, with the one asked for first selected, and the command line as defaults.
    for (const std::string& t : listTracks(paths)) {
        TrackStats ts;
        ts.file = ts.title = t;
        menu.tracks.push_back(ts);
        if (t == cfg.track) menu.track = (int)menu.tracks.size() - 1;
    }
    if (menu.tracks.empty() || menu.tracks[menu.track].file != cfg.track) {  // a path, or nothing found
        TrackStats ts;
        ts.file = ts.title = cfg.track;
        menu.tracks.insert(menu.tracks.begin(), ts);
        menu.track = 0;
    }
    menu.laps = cfg.laps == 3 ? 10 : cfg.laps;  // 3 is the headless default; a race to watch is longer
    menu.tankLitres = rr::CarParams{}.fuelCapacity;
    auto ensureStats = [&]() {
        TrackStats& ts = menu.tracks[menu.track];
        if (!ts.measured) ts = calibrate(ts.file, paths);
    };
    if (inMenu) {
        ensureStats();
        menu.setWearRate(cfg.wearRate);
    }

    auto race = makeRace(cfg, paths);
    if (!race) return 1;
    for (const auto& w : race->track().warnings()) std::fprintf(stderr, "track warning: %s\n", w.c_str());

    SetTraceLogLevel(LOG_WARNING);
    unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!shotMode) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    InitWindow(cfg.width, cfg.height, "Raylib Racers");
    if (cfg.fullscreen) ToggleFullscreen();
    SetTargetFPS(shotMode ? 0 : 120);
    SetExitKey(KEY_NULL);  // Esc goes back to the menu

    // The renderer and HUD are built for one track; a new track gets new ones.
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Hud> hud;
    std::string sceneTrack;
    auto buildScene = [&]() -> bool {
        if (renderer && sceneTrack == race->config().track) return true;
        if (hud) hud->shutdown();
        if (renderer) renderer->shutdown();
        renderer = std::make_unique<Renderer>();
        hud = std::make_unique<Hud>();
        if (!renderer->init(race->track(), (unsigned)cfg.seed, paths.assets, &err)) return false;
        hud->init(race->track(), paths.assets);
        sceneTrack = race->config().track;
        return true;
    };
    if (!buildScene()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        CloseWindow();
        return 1;
    }

    RaceAudio audio;
    if (!shotMode && !audio.init()) std::fprintf(stderr, "note: no audio device, running without sound\n");

    HudState st;
    st.timeScale = cfg.timeScale;
    st.muted = cfg.mute;
    st.camera = (CamMode)(std::max(0, cfg.camera) % CAM_COUNT);
    double simDebt = 0;
    int shotFrames = 0;
    std::vector<MenuHit> menuHits;

    if (shotMode && !inMenu)
        while (!race->cooledDown() && race->time() < cfg.screenshotAt) race->step();
    // --focus N picks a car; without it the camera follows whoever leads.
    if (cfg.focus >= 0 && cfg.focus < (int)race->cars().size()) {
        st.focus = cfg.focus;
        st.followLeader = false;
    }

    // Applies the menu's choices and starts a fresh race (or, for a new track, a fresh grid to look at).
    auto applyMenu = [&]() -> bool {
        const TrackStats& ts = menu.stats();
        cfg.track = ts.file;
        cfg.laps = menu.laps;
        cfg.wearRate = menu.wearRate();
        cfg.entries = gridEntries();
        auto fresh = makeRace(cfg, paths);
        if (!fresh) return false;
        race = std::move(fresh);
        simDebt = 0;
        st.focus = std::min(st.focus, (int)race->cars().size() - 1);
        if (!buildScene()) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return false;
        }
        return true;
    };
    if (inMenu) applyMenu();

    // ---- weekend: qualifying runs one car at a time, then the race starts in that order
    enum class Phase { Race, Quali, QualiDone } phase = Phase::Race;
    std::vector<rr::EntrySpec> weekendEntries;
    std::vector<float> qualiTime;
    int qualiCar = 0;
    auto qualiConfig = [&](int k) {
        rr::RaceConfig q = cfg;
        q.entries = {weekendEntries[k]};
        q.laps = 3;  // out lap and two flying laps
        q.fuelLimit = menu.stats().fuelPerLap * 3.6f;
        return q;
    };
    auto refreshQualiLines = [&]() {
        std::vector<int> idx(weekendEntries.size());
        for (int i = 0; i < (int)idx.size(); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            const bool ta = qualiTime[a] > 0, tb = qualiTime[b] > 0;
            if (ta != tb) return ta;
            return ta && qualiTime[a] < qualiTime[b];
        });
        st.quali.clear();
        for (int i : idx) {
            QualiLine l;
            l.name = weekendEntries[i].name;
            l.color = liveryTable().empty() ? teamColor(i) : liveryTable()[menu.carLivery[i]].color;
            l.time = qualiTime[i];
            l.running = phase == Phase::Quali && i == qualiCar;
            st.quali.push_back(l);
        }
        st.qualiRun = qualiCar + 1;
        st.qualiRuns = (int)weekendEntries.size();
        return idx;
    };
    auto startQualiRun = [&](int k) -> bool {
        qualiCar = k;
        setCarLiveries({menu.carLivery[k]});
        auto fresh = makeRace(qualiConfig(k), paths);
        if (!fresh) return false;
        race = std::move(fresh);
        simDebt = 0;
        st.focus = 0;
        st.qualifying = true;
        refreshQualiLines();
        return true;
    };
    // Ends the current run (simulating whatever is left of it), and with `all` the remaining runs too.
    auto finishQuali = [&](bool all) {
        while (!race->isOver()) race->step();
        qualiTime[qualiCar] = race->cars()[0].bestLap;
        while (all && qualiCar + 1 < (int)weekendEntries.size()) {
            ++qualiCar;
            auto run = makeRace(qualiConfig(qualiCar), paths);
            if (!run) break;
            while (!run->isOver()) run->step();
            qualiTime[qualiCar] = run->cars()[0].bestLap;
        }
        if (qualiCar + 1 < (int)weekendEntries.size()) {
            startQualiRun(qualiCar + 1);
        } else {
            phase = Phase::QualiDone;
            st.qualifying = false;
            refreshQualiLines();
        }
    };
    auto startWeekendRace = [&]() -> bool {
        std::vector<int> order = refreshQualiLines();
        cfg.entries.clear();
        std::vector<int> slots;
        for (int i : order) {
            cfg.entries.push_back(weekendEntries[i]);
            slots.push_back(menu.carLivery[i]);
        }
        setCarLiveries(slots);
        auto fresh = makeRace(cfg, paths);
        if (!fresh) return false;
        race = std::move(fresh);
        simDebt = 0;
        phase = Phase::Race;
        st.qualifying = false;
        st.followLeader = true;
        return true;
    };

    bool quit = false;
    while (!WindowShouldClose() && !quit) {
        const float frameDt = std::min(GetFrameTime(), 0.1f);
        const int n = (int)race->cars().size();

        if (inMenu) {
            // ---- race setup
            MenuAction act = updateMenu(menu, menuHits);
            if (act == MenuAction::Quit) quit = true;
            if (act == MenuAction::TrackChanged) {
                ensureStats();
                if (!applyMenu()) quit = true;
            }
            if (act == MenuAction::Start) {
                if (!applyMenu()) quit = true;
                inMenu = false;
                st.paused = false;
                if (menu.weekend && !quit) {
                    phase = Phase::Quali;
                    weekendEntries = cfg.entries;
                    qualiTime.assign(weekendEntries.size(), 0.0f);
                    if (!startQualiRun(0)) quit = true;
                }
            }
            renderer->updateCamera(*race, race->order()[0], CAM_CINEMATIC, frameDt);
        } else {
            // ---- input
            const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            auto pick = [&](int car) { st.focus = car; st.followLeader = false; };
            if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_RIGHT)) pick((st.focus + 1) % n);
            if (IsKeyPressed(KEY_LEFT)) pick((st.focus + n - 1) % n);
            for (int k = 0; k < 9 && k < n; ++k)
                if (IsKeyPressed(KEY_ONE + k)) pick(race->order()[k]);
            if (IsKeyPressed(KEY_L)) st.followLeader = !st.followLeader;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                const int car = hud->towerCarAt(*race, st, GetMousePosition());
                if (car >= 0) pick(car);
            }
            if (IsKeyPressed(KEY_C)) st.camera = (CamMode)((st.camera + (shift ? CAM_COUNT - 1 : 1)) % CAM_COUNT);
            for (int k = 0; k < CAM_COUNT && k < 7; ++k)
                if (IsKeyPressed(KEY_F2 + k)) st.camera = (CamMode)k;
            if (IsKeyPressed(KEY_SPACE)) st.paused = !st.paused;
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) st.timeScale = std::min(64.0f, st.timeScale * 2);
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) st.timeScale = std::max(0.125f, st.timeScale / 2);
            if (IsKeyPressed(KEY_P)) st.view.showPaths = !st.view.showPaths;
            if (IsKeyPressed(KEY_S)) st.view.showSensors = !st.view.showSensors;
            if (IsKeyPressed(KEY_M)) st.muted = !st.muted;
            if (IsKeyPressed(KEY_H)) st.showHud = !st.showHud;
            if (IsKeyPressed(KEY_F1)) st.showHelp = !st.showHelp;
            const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
            if (phase == Phase::Quali && enter) finishQuali(shift);
            else if (phase == Phase::QualiDone && enter && !startWeekendRace()) quit = true;
            if (IsKeyPressed(KEY_ESCAPE) && !shotMode) {
                inMenu = true;
                phase = Phase::Race;
                st.qualifying = false;
                applyMenu();  // back to the grid
            }
            if (IsKeyPressed(KEY_R) && phase == Phase::Race) {
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
                    for (long long i = 0; i < steps && !race->cooledDown(); ++i) race->step();
                } else if (IsKeyPressed(KEY_N)) {
                    race->advance(1.0 / race->config().robotHz);
                }
            }

            if (phase == Phase::Quali && race->isOver()) finishQuali(false);
            if (phase != Phase::Race) st.focus = 0;
            else if (st.followLeader) st.focus = race->order()[0];
            renderer->updateCamera(*race, st.focus, st.camera, shotMode ? 1.0f / 60 : frameDt);
        }
        // engine sound only while racing at (close to) real time
        audio.update(*race, renderer->camera, st.focus,
                     !inMenu && !st.paused && !st.muted && st.timeScale <= 2.0f && !race->cooledDown(), frameDt);

        BeginDrawing();
        ClearBackground(BLACK);
        renderer->draw(*race, inMenu ? race->order()[0] : st.focus, st.view);
        if (inMenu) hud->drawMenu(menu, menuHits);
        else if (phase == Phase::QualiDone) hud->drawQualiResults(st);
        else hud->draw(*race, st);
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
    audio.shutdown();
    hud->shutdown();
    renderer->shutdown();
    CloseWindow();
    return 0;
}
