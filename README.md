# Raylib Racers

A small, fast TORCS-style racing simulator for testing driving algorithms.
Cars are driven by **robots**: shared libraries written in C or C++ that read
sensors and return steering, throttle and brake. Races run headless at hundreds
of times real time for experiments, or in a raylib 3D viewer to watch them.

![Start of a race, TV camera](docs/images/start.jpg)

## What is in the box

- **Simulation core** (`src/core`, no graphics dependency): spline tracks, a
  planar dynamic-bicycle car model (load transfer, aero drag and downforce,
  tyre load sensitivity, friction circle, engine torque curve and gearbox),
  barrier and car-to-car collisions, lap timing and classification. Fixed 500 Hz
  physics, robots called at 50 Hz, fully deterministic.
- **Race mechanics**: fuel load and consumption, tyre wear with grip loss
  (soft, medium and hard compounds, each with a temperature window), dirty air, damage that costs downforce, power and grip, slipstream,
  and a pit lane with a speed limiter, a box per car and timed service (fuel,
  tyres, repairs). After the flag each car runs a slow lap into the pit lane
  and parks behind its box.
- **Robot API** (`include/rr/robot_api.h`): one C header. Sensors follow the
  TORCS SCR championship (angle, track position, 19 range finders, 36 opponent
  sectors) plus the full track geometry and the car's pose, as TORCS robots get,
  the car's fuel, tyre and pit state, and the nearest cars for racecraft.
  Robots can take parameters from the command line, print status text, log
  debug values to telemetry and draw a path in the viewer.
- **Three example robots** (`bots/`): `simple` (C, sensors only), `gapfollow`
  (C++, sensors only) and `racingline` (C++, plans a minimum-curvature line and
  speed profile, plans its own pit strategy from the timing screen, overtakes
  and defends).
- **`rr_race`**: headless runner with JSON results and per-car CSV telemetry.
  A 5-car, 3-lap race on the 3.2 km circuit takes about one second.
- **`rr_viewer`**: raylib 3D viewer with low-poly F1 cars in team liveries
  (steering, rolling wheels), sun shadows, fog, procedural textures, kerbs,
  barriers, pit lane and boxes, scenery, seven cameras, a timing
  tower (with tyres and pit status), minimap and a per-car panel with fuel and
  tyre wear.

| | |
|---|---|
| ![Cinematic camera](docs/images/cinematic.jpg) | ![Helicopter camera](docs/images/helicopter.jpg) |
| ![Orbit camera](docs/images/orbit.jpg) | ![TV camera](docs/images/tv.jpg) |
| ![Pit stop: the car is held in its box while the crew works](docs/images/pitstop.jpg) | ![Pit lane: speed limiter on, heading for the box](docs/images/pitlane.jpg) |

## Build

Needs CMake 3.16+ and a C++17 compiler. The viewer needs raylib 5.5: if it is
not installed, CMake downloads and builds it.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # quick smoke races
```

- **Linux**: raylib needs the X11/OpenGL headers, e.g. on Debian/Ubuntu
  `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.
- **Headless only** (servers, CI, sweeps): `-DRR_BUILD_VIEWER=OFF` skips raylib entirely.
- macOS and Windows should work (the loader handles `.dylib`/`.dll`) but have
  not been tried yet.

## Run

```sh
./build/rr_viewer                                   # watch the example robots race
./build/rr_viewer --track oval --laps 5 --car racingline --car gapfollow
./build/rr_race --laps 3 --car racingline --car simple --json results.json
./build/rr_race --car racingline --params "grip=0.85,brake=0.7" --telemetry tel/
./build/rr_race --laps 25 --car racingline --car racingline --params "tires=1"   # full-length race with stops
./build/rr_viewer --laps 8 --wear-rate 6 --focus 0          # short race, tyres wear fast enough to force a stop
```

`--car` takes a robot name from `build/bots/` or a path to any robot library;
`--params`, `--name`, `--spec`, `--dev` and `--tires soft|medium|hard` (the
team's choice of starting tyres, overriding the robot's) apply to the car before
them. `--help` lists all options (noise on the range finders, physics step,
robot rate, time limit, `--fuel-rate` and `--wear-rate` multipliers,
`--ambient` temperature, `--two-compounds on|off|auto`, `--cool-down` to run on
until the cars have parked after the flag).

**Two-compound rule.** By default a race longer than 20 laps requires every car
to use two different compounds; a car that finishes without doing so gets 30 s
added. `--two-compounds on|off` forces it either way.

The viewer opens on a **race setup** menu: track, race length, tyre life
(in laps of the chosen track; the menu measures a lap first), number of cars
(up to 20: ten teams of two, each with its own number), the session, the tyre
rule and a **Grid** page where each car gets a livery, a driving algorithm (any
robot library in `bots/` shows up there) and its starting tyres (Auto lets the
algorithm choose). **Weekend** mode runs qualifying first:
each car goes out alone for an out lap and two flying laps, and the fastest
lap takes pole. `Enter` skips the current run, `Shift+Enter` the rest of
qualifying. `--no-menu` skips the menu. The default grid runs the four
`racingline` variants in turn: standard, aggressive (`grip=0.85,brake=0.75,
push=1.3,attack=1.4,heat=15`: brakes later, learns closer to the limit, follows
closer, goes for gaps sooner and runs its tyres hotter), safe (`grip=0.75`) and
steady (`grip=0.7,brake=0.6,heat=0`);
`gapfollow` and `simple` are still on the Grid page.

The engine sound is synthesised from each car's revs and throttle (a V10 with
overrun pops and a rev limiter), for the cars nearest the camera. `M` mutes it;
`rr_viewer --sound-test out.wav --at 20` writes 25 s of it to a file.

Click a car in the timing tower to watch it. Viewer keys: `Tab`/arrows change car, `1`-`9` focus by position, `L` goes back
to following the leader (the default), `C` / `Shift+C` cycle the cameras and
`F2`-`F8` pick one: follow, cinematic (eases between framings around the car),
TV, helicopter, top down, orbit, overview. The mouse wheel zooms the orbit,
helicopter and top-down cameras. `Space` pauses, `+`/`-` change speed
(up to 64x), `N` single-steps while paused, `R` restarts, `P` toggles robot
paths, `S` shows the focused car's range finders, `M` mutes, `Esc` returns to
the menu, `H` hides the HUD, `F1` help.

To grab a frame without a window manager (e.g. under `xvfb-run`):
`rr_viewer --screenshot shot.png --at 30 --camera 1`.

## Car physics

A planar car with per-wheel loads: weight and downforce per axle, longitudinal
load transfer between the axles and lateral transfer between left and right,
split by the roll stiffness (56% front), lagging the accelerations like a
sprung car. Each wheel has its own load-sensitive tyre (a magic-formula curve
peaking around 6° of slip) and friction circle, so a lightly loaded inside
rear spins first and the limited-slip diff hands some of its drive to the
outside wheel. Downforce has a balance that moves forward under braking and
fades when the car slides sideways. Robots see `grip_use` and `slip_angle`
per axle, so under- and oversteer show up in telemetry
(`--telemetry DIR` writes them per car).

Tyres have a temperature per axle and a working window per compound (soft
85-105 °C, medium 95-115, hard 105-125). Sliding and rolling heat them, the
airflow cools them; cold tyres lose grip and grain, overheated ones lose grip
and wear several times faster, so driving hard costs tyre life. They leave the
warmers at 80 °C, so the first lap and the out lap after a stop are slower.
Fuel weight costs about a second a lap from a full tank to an empty one (tyre
load sensitivity is measured against the dry car). A car within 40 m behind
another loses up to 10% of its downforce in the dirty air, mostly at the
front, while the slipstream (up to 60 m back) cuts its drag. The car panel in
the viewer shows each axle's tyre temperature: blue cold, green in the
window, amber and red hot.

The timing tower shows each car's compound, its age in laps and its number of
stops. When the focused car's algorithm publishes its plan, the car panel shows
the window of its next stop and the tyres it will fit, and warns when the
two-compound rule still wants a second compound.

## Blue flags

A car about to be lapped gets a blue flag when the lapping car is within 60 m
(or 1.2 s) behind. Holding it up within 30 m for more than 8 s costs a 5 s
time penalty (once per lapping car), added to the race time. The timing tower shows blue-flagged
cars in blue and the car panel says who to let by.

## Car specs and team stats

Car numbers are data: `specs/f1_2006.json` lists every physics parameter of
the built-in car (any field left out keeps the default), and
`--spec FILE|NAME` gives a car another spec.

On top of the spec, each team rates its car in eight stats from 0 to 10, where
5 is the stock car and a team has 40 points in all, so raising one stat means
lowering another. `specs/development.json` defines them. Each point away from
5 changes the car linearly:

| Stat | Per point | 0 to 10 is worth |
|---|---|---|
| Tyre management | wear -5%, sliding heat -2% | tyre wear from +25% to -25% |
| Top speed | drag -0.8% | about 0.3 s a lap |
| Acceleration | engine torque +0.9% | about 0.25 s a lap |
| Downforce | downforce +0.6%, drag +0.2% | about 0.25 s a lap |
| Handling | mechanical grip +0.15%, yaw inertia -0.4% | about 0.3 s a lap |
| Pit stop speed | service time -4% | stops 20% longer to 20% shorter |
| Fuel efficiency | fuel per lap -1.6% | fuel use from +8% to -8% |
| Brakes | brake force +3% | little on Circuit Raylib, which has few big stops |

The lap times are on Circuit Raylib, so a full 0-to-10 swing in one stat is a
few tenths a lap, the gap between neighbouring top teams.
`--dev "top_speed=8,downforce=3"` sets one car's stats (stats left out stay at
5); over 40 points, outside 0-10 or an unknown stat is an error. The car's
spec reaches its robot through `RRCarSpec`, so planners adapt to it.

In the viewer the **Team stats** page edits each team's stats. Teammates share
them, and choosing a driving style for a driver on the Grid page gives the team
that style's stats (the aggressive racingline wants tyre management, the steady
one spends on speed). **Drivers per team** races one or two cars per team.

```sh
./build/rr_race --laps 25 --car racingline --dev "top_speed=8,handling=8,tire_management=2,pit_stop=2" \
                          --car racingline --dev "tire_management=8,fuel_efficiency=7,top_speed=3,downforce=2"
```

## Writing a robot

See [docs/ROBOTS.md](docs/ROBOTS.md). In short: export
`rr_robot_entry()` returning an `RRRobotApi` with `create`, `drive` and
`destroy`; build it as a shared library; pass it with `--car path/to/bot.so`.

## Tracks

Tracks are text files (`tracks/*.trk`): a name, a default width, the runoff to
the barrier, an optional pit lane and a list of control points in race order,
joined by a Catmull-Rom spline. A control point can carry its own width. The loader warns
when a corner is tighter than the track is wide or when the track overlaps
itself.

```
name My Track
width 14
runoff 7
pit left 20 80 420 500   # side, entry, lane start, lane end, exit (metres along the track)
pitspeed 22              # pit lane speed limit, m/s
p 0 0
p 300 0
p 400 120 16     # wider here
...
```

## Layout

```
include/rr/robot_api.h   robot ABI (C)
src/core/                track, car physics, race, robot loader, CLI, car specs
apps/headless/           rr_race
apps/viewer/             rr_viewer (renderer, HUD)
bots/                    example robots and shared helpers
tracks/                  circuit.trk, oval.trk
specs/                   car specs and development rules (JSON)
assets/fonts/            DejaVu fonts for the HUD (see DEJAVU_LICENSE.txt)
assets/cars/f1_gearari/  F1 car: body and wheel glTF, car.json, liveries (see its README)
```

## Current limits and next steps

- Flat tracks only (no elevation or banking), one car model.
- Racecraft is basic: overtaking between closely matched cars is rare. `gapfollow` and `simple` never pit, so in long
  races they run out of fuel or tyres.
- Possible next steps: more tracks and a track editor, per-team car setups,
  batch tournaments and parameter sweeps with a summary report, a Python
  binding for learning-based drivers, replay files, and a TORCS track importer.
